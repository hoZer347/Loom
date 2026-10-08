#include "DialogueManager.h"

#include "DialogueStates.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <regex>


namespace Loom
{
	// Splits a script into [Binding, Arg...] commands, <markup> and the plain text between.
	static const std::regex& TokenRegex()
	{
		static const std::regex regex(R"(\[[^\]]*\]|<[^>]*>)");

		return regex;
	};

	DialogueManager::DialogueManager()
	{
		_textSpeed = startingTextSpeed;

		#pragma region Default Bindings

		// A script's [Pause] maps to Pause(); [TextSpeed, 0.1] passes the argument.

		Bind("Proceed", [this]() { Proceed(); });

		Bind("Clear", [this]() { ClearText(); });

		Bind("Pause", [this]() { PushFirst<St_Dg_WaitForInput>(); });

		Bind("TextSpeed", [this](float speed) { _textSpeed = speed; });

		Bind("TextSpeed",
			[this](std::string speed)
			{
				if (speed == "Slow") _textSpeed = startingTextSpeed * 2.0f;
				else if (speed == "Normal") _textSpeed = startingTextSpeed;
				else if (speed == "Fast") _textSpeed = startingTextSpeed / 2.0f;
				else if (speed == "Default") _textSpeed = startingTextSpeed;
				else std::cerr << "[Dialogue] Unrecognized text speed '" << speed << "'." << std::endl;
			});

		#pragma endregion
	};

	#pragma region Script

	void DialogueManager::SetScript(std::string script)
	{
		_script = std::move(script);

		// Readiness was established against the previous script.
		_ready = false;
	};

	#pragma endregion

	#pragma region Text

	void DialogueManager::SetContinueText(std::string text)
	{
		_continueText = std::move(text);
	};

	void DialogueManager::Append(const std::string& text)
	{
		if (text.empty())
			return;

		_text += text;

		if (onTextChanged)
			onTextChanged(_text);
	};

	void DialogueManager::Append(char c)
	{
		_text += c;

		if (onTextChanged)
			onTextChanged(_text);
	};

	void DialogueManager::ClearText()
	{
		if (_text.empty())
			return;

		_text.clear();

		if (onTextChanged)
			onTextChanged(_text);
	};

	#pragma endregion

	#pragma region Voice

	float DialogueManager::VoiceOut() const
	{
		const float level = VoiceLevel ? std::clamp(VoiceLevel(), 0.0f, 1.0f) : 1.0f;

		return voiceVolume * level;
	};

	void DialogueManager::Speak(char c)
	{
		if (!onSpeak || std::isspace(static_cast<unsigned char>(c)))
			return;

		if (_voiceCharCount++ % std::max(1, voiceEvery) != 0)
			return;

		const float level = VoiceOut();

		if (level <= 0.0f)
			return;

		const float t = float(std::rand()) / float(RAND_MAX);

		onSpeak(c, level, voicePitchMin + (voicePitchMax - voicePitchMin) * t);
	};

	#pragma endregion

	#pragma region Running

	void DialogueManager::OnStart()
	{
		Build();

		// Fills the queue for an auto-begin dialogue, but never clobbers a Begin() that
		// ran first.
		if (_ready && !_begun)
			QueueScript();
	};

	void DialogueManager::Build()
	{
		if (_ready)
			return;

		if (_script.empty())
		{
			std::cerr << "[Dialogue] - Script is not assigned." << std::endl;

			Disable();

			return;
		};

		if (!onTextChanged)
			std::cerr
				<< "[Dialogue] No text sink assigned -- the streamed line will only be "
				   "readable through Text()."
				<< std::endl;

		_ready = true;
	};

	void DialogueManager::Begin()
	{
		Build();

		if (!_ready)
			return;

		_begun = true;

		QueueScript();

		Proceed();
	};

	void DialogueManager::Begin(const std::string& text)
	{
		Build();

		if (!_ready)
			return;

		_begun = true;

		Queue(text);

		Proceed();
	};

	void DialogueManager::QueueScript()
	{
		Queue(_script);
	};

	void DialogueManager::Queue(const std::string& text)
	{
		_textSpeed = startingTextSpeed;

		ClearText();

		// Qualified deliberately: this must empty the machine's queue, not the text box,
		// which ClearText() above already did. Without it a second Begin() would append
		// its states behind whatever was left of the first.
		StateMachineBase::Clear();

		for (const std::string& split : Tokens(text))
		{
			if (IsBinding(split))
			{
				const std::string content = split.substr(1, split.size() - 2);

				Push<St_Dg_InvokeBinding>()->binding =
					[this, content]() { InvokeBinding(content); };
			}
			else if (IsRichText(split))
				Push<St_Dg_StreamRichText>()->text = split;
			else Push<St_Dg_StreamPlainText>()->text = split;
		};

		Push<St_Dg_DialogueEnd>();
	};

	#pragma endregion

	#pragma region Bindings

	std::string DialogueManager::Lowered(const std::string& s)
	{
		std::string lowered;
		lowered.reserve(s.size());

		for (const char c : s)
			lowered += char(std::tolower(static_cast<unsigned char>(c)));

		return lowered;
	};

	void DialogueManager::Unbind(const std::string& name)
	{
		_bindings.erase(Lowered(name));
	};

	void DialogueManager::InvokeBinding(const std::string& content)
	{
		const size_t comma = content.find(',');

		const std::string name =
			DialogueArgs::Trim(comma == std::string::npos ? content : content.substr(0, comma));

		const std::string argList = comma == std::string::npos ? "" : content.substr(comma + 1);

		const auto found = _bindings.find(Lowered(name));

		if (found == _bindings.end())
		{
			std::cerr << "[Dialogue] No binding named '" << name << "'." << std::endl;

			return;
		};

		const std::vector<std::string> args = DialogueArgs::Split(argList);

		for (const auto& form : found->second)
			if (form(args))
				return;

		std::cerr
			<< "[Dialogue] No form of '" << name << "' takes the arguments '"
			<< argList << "'." << std::endl;
	};

	#pragma endregion

	#pragma region Parsing

	bool DialogueManager::IsBinding(const std::string& token)
	{
		return token.size() >= 2 && token.front() == '[' && token.back() == ']';
	};

	bool DialogueManager::IsRichText(const std::string& token)
	{
		return token.size() >= 2 && token.front() == '<' && token.back() == '>';
	};

	bool DialogueManager::IsClearBinding(const std::string& token)
	{
		const std::string content = token.substr(1, token.size() - 2);
		const size_t comma = content.find(',');

		return Lowered(
			DialogueArgs::Trim(comma == std::string::npos ? content : content.substr(0, comma)))
			== "clear";
	};

	static bool IsAllWhitespace(const std::string& s)
	{
		for (const char c : s)
			if (!std::isspace(static_cast<unsigned char>(c)))
				return false;

		return true;
	};

	static std::string TrimStart(const std::string& s)
	{
		size_t begin = 0;

		while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
			begin++;

		return s.substr(begin);
	};

	std::vector<std::string> DialogueManager::Tokens(const std::string& text)
	{
		// Every piece the regex carves the script into, the matches and the text between
		// them alike, in order.
		std::vector<std::string> pieces;

		auto begin = std::sregex_iterator(text.begin(), text.end(), TokenRegex());
		const auto end = std::sregex_iterator();

		size_t consumed = 0;

		for (auto it = begin; it != end; ++it)
		{
			const std::smatch& match = *it;

			pieces.push_back(text.substr(consumed, size_t(match.position()) - consumed));
			pieces.push_back(match.str());

			consumed = size_t(match.position()) + size_t(match.length());
		};

		pieces.push_back(text.substr(consumed));

		std::vector<std::string> tokens;

		// An emptied box takes no leading whitespace, or the file's line breaks stream as
		// text.
		bool pageStart = true;

		for (const std::string& split : pieces)
		{
			if (split.empty() || IsAllWhitespace(split))
				continue;

			if (IsBinding(split))
			{
				tokens.push_back(split);

				if (IsClearBinding(split))
					pageStart = true;

				continue;
			};

			if (IsRichText(split))
			{
				tokens.push_back(split);

				continue;
			};

			const std::string plain = pageStart ? TrimStart(split) : split;

			if (plain.empty())
				continue;

			tokens.push_back(plain);

			pageStart = false;
		};

		return tokens;
	};

	#pragma endregion
};

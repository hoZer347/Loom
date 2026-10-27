#include "DialogueManager.h"

#include "ComponentRegistry.h"
#include "DialogueStates.h"
#include "GameObject.h"
#include "RichText.h"
#include "Ui.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>


namespace Loom
{
	// Splits a script into [Name, Arg...] commands and the plain text between.
	static const std::regex& TokenRegex()
	{
		static const std::regex regex(R"(\[[^\]]*\])");

		return regex;
	};

	std::function<void(const std::string& type)> DialogueManagerBase::onNewBinding{ };

	DialogueManagerBase::DialogueManagerBase()
	{
		_textSpeed = startingTextSpeed;
	};

	#pragma region Default Bindings

	void DialogueManagerBase::WaitForInput()
	{
		PushFirst<St_Dg_WaitForInput>();
	};

	void DialogueManagerBase::SetTextSpeed(const std::string& speed)
	{
		// Each named speed against the one a line starts at.
		constexpr float SLOWER = 2.0f;
		constexpr float FASTER = 0.5f;

		if (speed == "Slow") _textSpeed = startingTextSpeed * SLOWER;
		else if (speed == "Normal") _textSpeed = startingTextSpeed;
		else if (speed == "Fast") _textSpeed = startingTextSpeed * FASTER;
		else if (speed == "Default") _textSpeed = startingTextSpeed;
		else std::cerr << "[Dialogue] Unrecognized text speed '" << speed << "'." << std::endl;
	};

	#pragma endregion

	#pragma region Running

	void DialogueManagerBase::SetScript(std::string script)
	{
		_script = std::move(script);

		// Readiness was established against the previous script.
		_ready = false;
	};

	void DialogueManagerBase::OnStart()
	{
		Build();

		// Starts an auto-begin dialogue, but never clobbers a Begin() that ran first.
		if (!_ready || _begun)
			return;

		_begun = true;

		QueueScript();

		Proceed();
	};

	void DialogueManagerBase::Build()
	{
		if (_ready)
			return;

		if (const std::string path = DialogueFile(); _script.empty() && !path.empty())
		{
			std::ifstream file(path, std::ios::binary);

			if (!file)
				std::cerr << "[Dialogue] Could not open '" << path << "'." << std::endl;

			_script.assign(std::istreambuf_iterator<char>(file), { });
		};

		if (_script.empty())
		{
			std::cerr << "[Dialogue] Script is not assigned." << std::endl;

			Disable();

			return;
		};

		if (!onTextChanged && Box() == nullptr)
			std::cerr
				<< "[Dialogue] No text sink assigned -- the streamed line will only be "
				   "readable through Text()."
				<< std::endl;

		_ready = true;
	};

	void DialogueManagerBase::Begin()
	{
		Build();

		if (!_ready)
			return;

		_begun = true;

		QueueScript();

		Proceed();
	};

	void DialogueManagerBase::Begin(const std::string& text)
	{
		Build();

		if (!_ready)
			return;

		_begun = true;

		Queue(text);

		Proceed();
	};

	void DialogueManagerBase::QueueScript()
	{
		Queue(_script);
	};

	void DialogueManagerBase::Queue(const std::string& text)
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
			else Push<St_Dg_StreamPlainText>()->text = split;
		};

		Push<St_Dg_DialogueEnd>();
	};

	#pragma endregion

	#pragma region Text

	void DialogueManagerBase::SetContinueText(std::string text)
	{
		_continueText = std::move(text);
	};

	void DialogueManagerBase::Append(const std::string& text)
	{
		if (text.empty())
			return;

		_text += text;

		Show();
	};

	void DialogueManagerBase::Append(char c)
	{
		_text += c;

		Show();
	};

	void DialogueManagerBase::ClearText()
	{
		if (_text.empty())
			return;

		_text.clear();

		Show();
	};

	void DialogueManagerBase::Show()
	{
		if (onTextChanged)
			onTextChanged(_text);
		else if (UiText* box = Box())
			box->text = _text;
	};

	UiText* DialogueManagerBase::Box() const
	{
		GameObject* object = Object();

		return object ? object->FindComponent<UiText>() : nullptr;
	};

	void DialogueManagerBase::AppendTag(std::string_view name, const std::string& arg)
	{
		Append('[' + std::string(name) + (arg.empty() ? "" : ", " + arg) + ']');
	};

	#pragma endregion

	#pragma region Voice

	float DialogueManagerBase::VoiceOut() const
	{
		const float level = VoiceLevel ? std::clamp(VoiceLevel(), 0.0f, 1.0f) : 1.0f;

		return voiceVolume * level;
	};

	void DialogueManagerBase::Speak(char c)
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

	#pragma region Bindings

	std::string DialogueManagerBase::Lowered(const std::string& s)
	{
		std::string lowered;
		lowered.reserve(s.size());

		for (const char c : s)
			lowered += char(std::tolower(static_cast<unsigned char>(c)));

		return lowered;
	};

	void DialogueManagerBase::Unbind(const std::string& name)
	{
		_bindings.erase(Lowered(name));
	};

	std::vector<std::string> DialogueManagerBase::Commands() const
	{
		std::vector<std::string> names;

		for (const auto& [lowered, binding] : _bindings)
			names.push_back(binding.name);

		for (const DialogueBindings::Form& form : Declared())
			if (std::none_of(names.begin(), names.end(), [&form](const std::string& name) { return Lowered(name) == Lowered(form.name); }))
				names.push_back(form.name);

		std::sort(names.begin(), names.end());

		return names;
	};

	GameObject* DialogueManagerBase::Object() const
	{
		// GetGameObject is not const only because a machine hands out its object to
		// change.
		return const_cast<DialogueManagerBase*>(this)->GetGameObject();
	};

	std::vector<DialogueBindings::Form> DialogueManagerBase::Declared() const
	{
		const LoomObject* self = dynamic_cast<const LoomObject*>(this);
		std::vector<DialogueBindings::Form> forms = DialogueBindings::Of(self);

		for (const GameObject* at = Object(); at != nullptr; at = at->GetParent())
			for (const ComponentBase* component : at->GetComponents())
			{
				if (component == self)
					continue;

				const std::vector<DialogueBindings::Form> declared = DialogueBindings::Of(component);
				forms.insert(forms.end(), declared.begin(), declared.end());
			};

		return forms;
	};

	void DialogueManagerBase::InvokeBinding(const std::string& content)
	{
		const size_t comma = content.find(',');

		const std::string name =
			DialogueArgs::Trim(comma == std::string::npos ? content : content.substr(0, comma));

		const std::string argList = comma == std::string::npos ? "" : content.substr(comma + 1);

		const std::string lowered = Lowered(name);
		const std::vector<std::string> args = DialogueArgs::Split(argList);
		const auto found = _bindings.find(lowered);
		bool named = found != _bindings.end();

		if (named)
			for (const auto& form : found->second.forms)
				if (form(args))
					return;

		for (const DialogueBindings::Form& form : Declared())
		{
			if (Lowered(form.name) != lowered)
				continue;

			named = true;

			if ((*form.invoke)(args))
				return;
		};

		if (!named)
		{
			std::cerr << "[Dialogue] No binding named '" << name << "'." << std::endl;

			return;
		};

		std::cerr
			<< "[Dialogue] No form of '" << name << "' takes the arguments '"
			<< argList << "'." << std::endl;
	};

	#pragma endregion

	#pragma region Gui

	void DialogueManagerBase::DrawNewBinding()
	{
		const ComponentBase* component = dynamic_cast<const ComponentBase*>(this);

		ImGui::BeginDisabled(!onNewBinding || component == nullptr);

		if (ImGui::Button("New Binding", ImVec2(-FLT_MIN, 0.0f)))
			onNewBinding(ComponentRegistry::NameOf(*component));

		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Adds a DialogueBinding to this script and opens it in Visual Studio");
	};

	#pragma endregion

	#pragma region Parsing

	bool DialogueManagerBase::IsBinding(const std::string& token)
	{
		return token.size() >= 2 && token.front() == '[' && token.back() == ']';
	};

	bool DialogueManagerBase::IsRichText(const std::string& token)
	{
		return RichText::IsTag(token);
	};

	bool DialogueManagerBase::IsClearBinding(const std::string& token)
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

	std::vector<std::string> DialogueManagerBase::Tokens(const std::string& text)
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

		std::erase(pieces, std::string());

		std::vector<std::string> tokens;

		// An emptied box takes no leading whitespace, or the file's line breaks stream as
		// text.
		bool pageStart = true;

		for (size_t at = 0; at < pieces.size(); at++)
		{
			const std::string& split = pieces[at];

			// Line breaks between commands are the file's layout; spaces beside a RichText
			// tag are the line's own.
			const bool besideTag = (at > 0 && IsRichText(pieces[at - 1])) || (at + 1 < pieces.size() && IsRichText(pieces[at + 1]));

			if (IsAllWhitespace(split) && (!besideTag || split.find('\n') != std::string::npos))
				continue;

			if (IsBinding(split))
			{
				tokens.push_back(split);

				if (IsClearBinding(split))
					pageStart = true;

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

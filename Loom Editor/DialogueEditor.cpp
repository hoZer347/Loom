#include "DialogueEditor.h"

#include "EditorIcons.h"

// The editor only runs on Windows, and the web test build has no TextEditor.
#ifndef __EMSCRIPTEN__

#include "RichText.h"
#include "Utilities/DialogueArgs.h"
#include "Utilities/DialogueManager.h"

#include <algorithm>
#include <cctype>


namespace Loom
{
	namespace
	{
		constexpr char open_bracket = '[';
		constexpr char close_bracket = ']';
		constexpr char argument = ',';

		// Past it, a glyph is never part of a command's name or arguments.
		constexpr ImWchar last_ascii = 0x7F;

		std::string Lowered(std::string_view text)
		{
			std::string lowered(text);

			std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return (char)std::tolower(c); });

			return lowered;
		};

		TextEditor::Iterator Tokenize(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color)
		{
			if (start == end || *start != open_bracket)
				return start;

			std::string text;

			for (TextEditor::Iterator at = start; at != end && *at <= last_ascii; at++)
			{
				text += (char)*at;

				if (*at == close_bracket)
					break;
			};

			TextEditor::Iterator after = start;

			for (size_t length = DialogueEditor::Command(text, color); length > 0; length--)
				after++;

			return after;
		};

		const TextEditor::Language* Dialogue()
		{
			static const TextEditor::Language language = []()
			{
				TextEditor::Language dialogue;
				dialogue.name = "Dialogue";
				dialogue.customTokenizer = Tokenize;

				return dialogue;
			}();

			return &language;
		};
	};

	#pragma region Language

	const std::vector<std::string>& DialogueEditor::Commands()
	{
		static std::vector<std::string> commands{ };
		static unsigned long long version = 0;

		if (!commands.empty() && version == DialogueBindings::NamesVersion())
			return commands;

		version = DialogueBindings::NamesVersion();
		commands = Basic::DialogueManager().Commands();

		for (const std::string& name : DialogueBindings::Names())
			if (std::none_of(commands.begin(), commands.end(), [&name](const std::string& command) { return Lowered(command) == Lowered(name); }))
				commands.push_back(name);

		std::sort(commands.begin(), commands.end());

		return commands;
	};

	size_t DialogueEditor::Command(std::string_view text, TextEditor::Color& color)
	{
		const size_t close = text.find(close_bracket);

		if (text.empty() || text.front() != open_bracket || close == std::string_view::npos)
			return 0;

		const std::string token(text.substr(0, close + 1));

		if (RichText::IsTag(token))
		{
			color = TextEditor::Color::declaration;
			return token.size();
		};

		const std::string name = Lowered(DialogueArgs::Trim(token.substr(1, std::min(token.find(argument), close) - 1)));
		const std::vector<std::string>& commands = Commands();

		if (std::none_of(commands.begin(), commands.end(), [&name](const std::string& command) { return Lowered(command) == name; }))
			return 0;

		color = TextEditor::Color::keyword;
		return token.size();
	};

	#pragma endregion

	#pragma region Kind

	const char* DialogueEditor::Panel() const
	{
		return EditorPanels::Dialogues;
	};

	const char* DialogueEditor::Icon() const
	{
		return EditorIcons::Dialogue;
	};

	const char* DialogueEditor::Hint() const
	{
		return "No dialogue open";
	};

	const TextEditor::Language* DialogueEditor::Language() const
	{
		return Dialogue();
	};

	std::vector<ShaderMessage> DialogueEditor::Check(const Document&) const
	{
		return { };
	};

	void DialogueEditor::Suggest(const Document& document, TextEditor::AutoCompleteState& state) const
	{
		const TextEditor::DocPos start = state.searchTermStart;
		const std::string typed = Lowered(state.searchTerm);

		state.suggestions.clear();

		// Only a command's name, not the words a line says.
		if (start.index == 0 || document.editor.GetSectionText(TextEditor::DocPos(start.line, start.index - 1), start) != std::string(1, open_bracket))
			return;

		for (const std::string& command : Commands())
			if (command.size() > typed.size() && Lowered(command).starts_with(typed))
				state.suggestions.push_back(command);
	};

	#pragma endregion
};

#endif

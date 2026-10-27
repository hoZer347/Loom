#pragma once

#include "TextFileEditor.h"

#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::DialogueEditor
	* - The Dialogues panel: a tab for each open .dialogue file, the commands a
	*   DialogueManager runs and the RichText tags a UiText draws coloured
	*   where they are written as [Name, args], and completed by name
	* - A bracket naming neither is left as text, as a game's own bindings are
	*   bound where the editor cannot see them
	*/
	struct DialogueEditor final : TextFileEditor
	{
		#pragma region Language

		// A DialogueManager's bindings and the RichText tags, sorted.
		static const std::vector<std::string>& Commands();

		// How long the command text starts with is, brackets and all, with the
		// colour it is drawn in, or 0 when it does not start with one.
		static size_t Command(std::string_view text, TextEditor::Color& color);

		#pragma endregion

	protected:
		#pragma region Kind

		const char* Panel() const override;
		const char* Icon() const override;
		const char* Hint() const override;
		const TextEditor::Language* Language() const override;
		std::vector<ShaderMessage> Check(const std::string& text) const override;
		void Suggest(const Document& document, TextEditor::AutoCompleteState& state) const override;

		#pragma endregion
	};
};

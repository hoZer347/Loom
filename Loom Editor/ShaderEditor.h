#pragma once

#include "TextFileEditor.h"


namespace Loom
{
	/**
	* Loom::ShaderEditor
	* - The Shaders panel: a tab for each open .shader file, highlighted as
	*   GLSL, completing GLSL's words, the file's own and the engine's, and
	*   explaining the engine's on hover
	* - Checks the text with glslang a moment after each edit and marks what it
	*   says on the line it is about. Ctrl+S saves, and the engine reloads the
	*   shader from the file
	*/
	struct ShaderEditor final : TextFileEditor
	{
	protected:
		#pragma region Kind

		const char* Panel() const override;
		const char* Icon() const override;
		const char* Hint() const override;
		const TextEditor::Language* Language() const override;
		std::vector<ShaderMessage> Check(const std::string& text) const override;
		void Explain(const Document& document) const override;

		#pragma endregion
	};
};

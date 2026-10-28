#include "ShaderEditor.h"

#include "EditorIcons.h"

// The web test build compiles this too, and glslang is desktop only.
#ifndef __EMSCRIPTEN__

#include "EditorTheme.h"
#include "ShaderVariables.h"

#include "imgui.h"

#include <algorithm>
#include <iterator>
#include <regex>
#include <string_view>


namespace Loom
{
	namespace
	{
		// How wide an explanation runs before it wraps, in font heights.
		constexpr float explanation_width = 30.0f;

		const ShaderSymbol* EngineSymbol(std::string_view name)
		{
			// A kernel is described by its kind, whatever its size.
			static const std::regex kernel(R"(^(\w+Kernel)_\d+x\d+$)");
			std::match_results<std::string_view::const_iterator> sized;

			if (std::regex_match(name.begin(), name.end(), sized, kernel))
				name = std::string_view(sized[1].first, sized[1].second);

			const auto found = std::find_if(
				std::begin(ENGINE_SHADER_SYMBOLS),
				std::end(ENGINE_SHADER_SYMBOLS),
				[name](const ShaderSymbol& symbol) { return symbol.name == name; });

			return found == std::end(ENGINE_SHADER_SYMBOLS) ? nullptr : &*found;
		};

		// GLSL, with the engine's names coloured as GLSL's own are.
		const TextEditor::Language* Glsl()
		{
			static const TextEditor::Language language = []()
			{
				TextEditor::Language glsl = *TextEditor::Language::Glsl();

				for (const ShaderSymbol& symbol : ENGINE_SHADER_SYMBOLS)
					glsl.identifiers.emplace(symbol.name);

				return glsl;
			}();

			return &language;
		};
	};

	#pragma region Kind

	const char* ShaderEditor::Panel() const
	{
		return EditorPanels::Shaders;
	};

	const char* ShaderEditor::Icon() const
	{
		return EditorIcons::Shader;
	};

	const char* ShaderEditor::Hint() const
	{
		return "No shader open";
	};

	const TextEditor::Language* ShaderEditor::Language() const
	{
		return Glsl();
	};

	std::vector<ShaderMessage> ShaderEditor::Check(const Document& document) const
	{
		const std::string text = document.editor.GetText();

		return Shader::IsPixel(document.path) ? Shader::CheckPixel(text) : Shader::Check(text);
	};

	void ShaderEditor::Explain(const Document& document) const
	{
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			return;

		const ShaderSymbol* symbol = EngineSymbol(document.editor.GetWordAtMousePos(ImGui::GetMousePos()));

		if (symbol == nullptr || !ImGui::BeginTooltip())
			return;

		ImGui::PushFont(EditorTheme::CodeFont(), 0.0f);
		ImGui::TextUnformatted(symbol->declaration.data(), symbol->declaration.data() + symbol->declaration.size());
		ImGui::PopFont();

		ImGui::PushTextWrapPos(ImGui::GetFontSize() * explanation_width);
		ImGui::TextUnformatted(symbol->description.data(), symbol->description.data() + symbol->description.size());
		ImGui::PopTextWrapPos();

		ImGui::EndTooltip();
	};

	#pragma endregion
};

#endif

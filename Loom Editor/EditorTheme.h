#pragma once

#include "imgui.h"

#include <string>
#include <vector>


namespace Loom
{
	namespace EditorTheme
	{
		#pragma region Fonts

		// The font the editor starts in, and the sizes the Settings window offers.
		constexpr const char* DefaultFont = "Roboto-Medium.ttf";
		constexpr float DefaultFontSize = 15.0f;
		constexpr float MinFontSize = 8.0f;
		constexpr float MaxFontSize = 32.0f;

		// The fonts vendored with ImGui, by file name.
		std::vector<std::string> Fonts();

		// Makes the named font the atlas's first, so the one ImGui draws with,
		// and the code font the only other. ImGui's built-in ProggyClean stands
		// in for a missing file. Not between NewFrame and Render, when the atlas
		// is locked.
		void LoadFont(const std::string& file, float size);

		// Monospaced, at the size LoadFont was given.
		ImFont* CodeFont();

		#pragma endregion

		#pragma region Style

		// Replaces the engine's stock ImGui sizes and colours with the editor's.
		void ApplyStyle();

		// Text colours for the panels, picked to read on the theme's backgrounds.
		extern const ImVec4 AccentText;
		extern const ImVec4 ErrorText;
		extern const ImVec4 WarningText;
		extern const ImVec4 SuccessText;

		#pragma endregion
	};
};

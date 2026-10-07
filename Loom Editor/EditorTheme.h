#pragma once

#include "imgui.h"

#include <string>
#include <vector>


namespace Loom
{
	namespace EditorTheme
	{
		// The font the editor starts in, and the sizes the Settings window offers.
		constexpr const char* DefaultFont = "Roboto-Medium.ttf";
		constexpr float DefaultFontSize = 15.0f;
		constexpr float MinFontSize = 8.0f;
		constexpr float MaxFontSize = 32.0f;

		// Replaces the engine's stock ImGui sizes and colours with the editor's.
		void ApplyStyle();

		// The fonts vendored with ImGui, by file name.
		std::vector<std::string> Fonts();

		// Makes the named font the only one in the atlas. ImGui's built-in
		// ProggyClean stands in when the file is missing. Not between NewFrame
		// and Render, when the atlas is locked; the backend's font texture has
		// to be rebuilt after (Renderer::ReleaseImGuiFonts).
		void LoadFont(const std::string& file, float size);

		// Text colours for the panels, picked to read on the theme's backgrounds.
		extern const ImVec4 AccentText;
		extern const ImVec4 ErrorText;
	};
};

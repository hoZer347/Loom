#pragma once

#include "EditorTheme.h"
#include "Renderer.h"

#include <ostream>
#include <string>


namespace Loom
{
	/**
	* Loom::EditorSettings
	* - What the editor remembers that belongs to no project: the graphics API
	*   it runs on, its font and how ImGui looks
	* - Kept as key=value lines in the editor's settings file, and changed in
	*   the Settings window (EditorSettingsWindow.cpp, which also puts them
	*   into ImGui)
	*/
	struct EditorSettings final
	{
		enum class Theme { Loom, Dark, Light, Classic, Count };

		static const char* BackendLabel(Backend backend);

		// Takes a line of the settings file, if it is one of these.
		void Read(const std::string& line);
		void Write(std::ostream& out) const;

		// Puts the font and the look into ImGui. Before the first frame, since
		// the font atlas is locked during one; the window applies its changes
		// between frames.
		void Apply() const;

		// The Settings window. True when something changed that is worth saving.
		bool Draw(bool* open);

		// What the next session starts on. A change waits for a restart, since
		// the window was opened for the API it is on.
		Backend backend = Backend::OpenGL;

		std::string font = EditorTheme::DefaultFont;
		float fontSize = EditorTheme::DefaultFontSize;

		Theme theme = Theme::Loom;

		// Of every size ImGui draws with, text included.
		float scale = 1.0f;

		static constexpr float MinScale = 0.5f;
		static constexpr float MaxScale = 2.0f;

	private:
		void ApplyLook() const;

		// Applied once the frame being built has been drawn.
		void QueueFont() const;
		void QueueLook() const;

		bool DrawGraphics();
		bool DrawFonts();
		bool DrawInterface();
	};
};

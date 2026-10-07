#include "EditorSettings.h"

#include <algorithm>
#include <cstdlib>


namespace Loom
{
	namespace
	{
		constexpr const char* BACKEND_KEY = "backend";
		constexpr const char* FONT_KEY = "font";
		constexpr const char* FONT_SIZE_KEY = "font_size";
		constexpr const char* THEME_KEY = "theme";
		constexpr const char* SCALE_KEY = "ui_scale";

		const char* const BACKEND_VALUES[] = { "opengl", "vulkan" };
		const char* const THEME_VALUES[] = { "loom", "dark", "light", "classic" };

		// Whichever of values the text names, or fallback.
		template<typename Enum, size_t N>
		Enum Named(const std::string& text, const char* const (&values)[N], Enum fallback)
		{
			const auto found = std::find(std::begin(values), std::end(values), text);

			return found == std::end(values) ? fallback : (Enum)(found - std::begin(values));
		};

		float Number(const std::string& text, float fallback, float min, float max)
		{
			// strtof rather than from_chars, which the web's libc++ has no float overload of.
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);

			return end != text.c_str() ? std::clamp(value, min, max) : fallback;
		};
	};

	const char* EditorSettings::BackendLabel(Backend backend)
	{
		return backend == Backend::Vulkan ? "Vulkan" : "OpenGL";
	};

	void EditorSettings::Read(const std::string& line)
	{
		const size_t equals = line.find('=');

		if (equals == std::string::npos)
			return;

		const std::string key = line.substr(0, equals);
		const std::string value = line.substr(equals + 1);

		if (key == BACKEND_KEY)
			backend = Named(value, BACKEND_VALUES, Backend::OpenGL);
		else if (key == FONT_KEY)
			font = value;
		else if (key == FONT_SIZE_KEY)
			fontSize = Number(value, EditorTheme::DefaultFontSize, EditorTheme::MinFontSize, EditorTheme::MaxFontSize);
		else if (key == THEME_KEY)
			theme = Named(value, THEME_VALUES, Theme::Loom);
		else if (key == SCALE_KEY)
			scale = Number(value, 1.0f, MinScale, MaxScale);
	};

	void EditorSettings::Write(std::ostream& out) const
	{
		out << BACKEND_KEY << '=' << BACKEND_VALUES[(int)backend] << '\n';
		out << FONT_KEY << '=' << font << '\n';
		out << FONT_SIZE_KEY << '=' << fontSize << '\n';
		out << THEME_KEY << '=' << THEME_VALUES[(int)theme] << '\n';
		out << SCALE_KEY << '=' << scale << '\n';
	};
};

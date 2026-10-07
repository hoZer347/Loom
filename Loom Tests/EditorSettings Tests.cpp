#include "doctest.h"

#include "EditorSettings.h"

#include <sstream>
#include <string>


namespace
{
	// Reads every line of text into a fresh set of settings.
	Loom::EditorSettings ReadAll(const std::string& text)
	{
		Loom::EditorSettings settings;
		std::istringstream in(text);
		std::string line;

		while (std::getline(in, line))
			settings.Read(line);

		return settings;
	};
};


TEST_SUITE("EditorSettings")
{
	TEST_CASE("what is written reads back the same")
	{
		Loom::EditorSettings written;
		written.backend = Loom::Backend::Vulkan;
		written.font = "Cousine-Regular.ttf";
		written.fontSize = 20.0f;
		written.theme = Loom::EditorSettings::Theme::Light;
		written.scale = 1.25f;

		std::ostringstream out;
		written.Write(out);

		const Loom::EditorSettings read = ReadAll(out.str());

		CHECK(read.backend == written.backend);
		CHECK(read.font == written.font);
		CHECK(read.fontSize == doctest::Approx(written.fontSize));
		CHECK(read.theme == written.theme);
		CHECK(read.scale == doctest::Approx(written.scale));
	};

	TEST_CASE("an unknown backend or theme falls back to the default")
	{
		const Loom::EditorSettings defaults;
		const Loom::EditorSettings read = ReadAll("backend=metal\ntheme=neon\n");

		CHECK(read.backend == defaults.backend);
		CHECK(read.theme == defaults.theme);
	};

	TEST_CASE("sizes are clamped to what the Settings window offers")
	{
		const Loom::EditorSettings small = ReadAll("font_size=1\nui_scale=0.01\n");

		CHECK(small.fontSize == doctest::Approx(Loom::EditorTheme::MinFontSize));
		CHECK(small.scale == doctest::Approx(Loom::EditorSettings::MinScale));

		const Loom::EditorSettings large = ReadAll("font_size=500\nui_scale=40\n");

		CHECK(large.fontSize == doctest::Approx(Loom::EditorTheme::MaxFontSize));
		CHECK(large.scale == doctest::Approx(Loom::EditorSettings::MaxScale));
	};

	TEST_CASE("a value that is not a number leaves the default")
	{
		const Loom::EditorSettings defaults;
		const Loom::EditorSettings read = ReadAll("font_size=big\nui_scale=\n");

		CHECK(read.fontSize == doctest::Approx(defaults.fontSize));
		CHECK(read.scale == doctest::Approx(defaults.scale));
	};

	TEST_CASE("lines that are not settings are ignored")
	{
		const Loom::EditorSettings defaults;
		const Loom::EditorSettings read = ReadAll("project=C:/somewhere\nno equals sign\n=\n");

		CHECK(read.backend == defaults.backend);
		CHECK(read.font == defaults.font);
	};
};

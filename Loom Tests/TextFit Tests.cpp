#include "doctest.h"

#include "TextFit.h"

#include "imgui.h"

#include <cfloat>
#include <string>

namespace
{
	// An ImGui context of the test's own, mid-frame, so text can be measured
	// in its default font.
	struct Measuring
	{
		Measuring()
		{
			constexpr float frame_time = 1.0f / 60.0f;
			constexpr float display_size = 800.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(display_size, display_size);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			ImGui::NewFrame();
		};

		~Measuring()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(context);
		};

		ImGuiContext* context = nullptr;
	};

	const std::string path = "C:\\Users\\Someone\\Desktop\\Game";

	float Width(const std::string& text)
	{
		return ImGui::CalcTextSize(text.c_str()).x;
	};
};


TEST_SUITE("TextFit")
{
	TEST_CASE("text that fits comes back whole")
	{
		Measuring measuring;

		CHECK(Loom::FitTail(path, FLT_MAX) == path);
		CHECK(Loom::FitTail(path, Width(path)) == path);
	};

	TEST_CASE("text that does not fit keeps as much of its end as fits behind the ellipsis")
	{
		Measuring measuring;

		const std::string tail = "Desktop\\Game";
		const float width = Width(Loom::ellipsis + tail);
		const std::string shown = Loom::FitTail(path, width);

		CHECK(shown == Loom::ellipsis + tail);
		CHECK(Width(shown) <= width);
	};

	TEST_CASE("a width too narrow for anything more leaves the ellipsis alone")
	{
		Measuring measuring;

		CHECK(Loom::FitTail(path, Width(Loom::ellipsis)) == Loom::ellipsis);
		CHECK(Loom::FitTail(path, 0.0f) == Loom::ellipsis);
	};

	TEST_CASE("a path that does not fit is cut between folders, keeping as many as fit")
	{
		Measuring measuring;

		const std::string kept = "\\Desktop\\Game";

		CHECK(Loom::FitPath(path, FLT_MAX) == path);
		CHECK(Loom::FitPath(path, Width(Loom::ellipsis + kept)) == Loom::ellipsis + kept);
		CHECK(Loom::FitPath(path, Width(Loom::ellipsis + kept) - 1.0f) == Loom::ellipsis + std::string("\\Game"));

		const std::string tank = Loom::ellipsis + std::string("/Tank.loomprefab");

		CHECK(Loom::FitPath("Assets/Prefabs/Tank.loomprefab", Width(tank)) == tank);
	};

	TEST_CASE("a path whose file's name alone does not fit keeps the end of the name")
	{
		Measuring measuring;

		const std::string name = "Assets/Prefabs/Headquarters.loomprefab";
		const float width = Width(Loom::ellipsis + std::string("prefab"));

		CHECK(Loom::FitPath(name, width) == Loom::FitTail(name, width));
	};

	TEST_CASE("a character spelled in several bytes is never cut")
	{
		Measuring measuring;

		// What the font has no glyph for, the stray bytes of a cut character
		// among them, narrower than é, so a tail starting inside é would fit
		// where é itself does not.
		constexpr float narrow_fallback = 1.0f;
		ImGui::GetFontBaked()->FallbackAdvanceX = narrow_fallback;

		// é, then ✓, two and three bytes long in UTF-8.
		const std::string text = "Caf\xC3\xA9 \xE2\x9C\x93 Game";
		constexpr unsigned char continuation_mask = 0xC0;
		constexpr unsigned char continuation = 0x80;
		constexpr float step = 1.0f;

		for (float width = 0.0f; width <= Width(text); width += step)
		{
			const std::string shown = Loom::FitTail(text, width);
			const std::string kept = shown.substr(shown.starts_with(Loom::ellipsis) ? std::string(Loom::ellipsis).size() : 0);

			CAPTURE(width);
			CHECK(text.ends_with(kept));
			CHECK((kept.empty() || ((unsigned char)kept[0] & continuation_mask) != continuation));
		};
	};
};

#include "doctest.h"

#include "Test Support.h"

#include "SpriteEditor.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>


namespace
{
	using Loom::SpriteEditor;

	constexpr int RGBA = 4;
	constexpr uint8_t FULL = 255;

	constexpr SpriteEditor::Colour RED = { FULL, 0, 0, FULL };
	constexpr SpriteEditor::Colour GREEN = { 0, FULL, 0, FULL };
	constexpr SpriteEditor::Colour CLEAR = { 0, 0, 0, 0 };

	SpriteEditor::Image Blank(int width, int height, const SpriteEditor::Colour& colour = CLEAR)
	{
		SpriteEditor::Image image{ width, height, { } };

		for (int i = 0; i < width * height; i++)
			image.pixels.insert(image.pixels.end(), colour.begin(), colour.end());

		return image;
	};

	int Count(const SpriteEditor::Image& image, const SpriteEditor::Colour& colour)
	{
		int count = 0;

		for (int y = 0; y < image.height; y++)
			for (int x = 0; x < image.width; x++)
				count += image.Get(x, y) == colour;

		return count;
	};

	// The Sprites panel in an ImGui context of the test's own, with no window
	// and no renderer, filling the display with one small image open.
	struct Driven
	{
		static constexpr float width = 900.0f;
		static constexpr float height = 600.0f;

		// The image is fitted in the middle of the canvas, as large as it goes.
		static constexpr int side = 4;

		// Enough for the panel to dock its tab and take focus.
		static constexpr int opening_frames = 4;

		explicit Driven(const std::string& name) :
			path(LoomTests::WriteTga(name, side, side, Blank(side, side).pixels))
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			editor.Open(path);

			for (int i = 0; i < opening_frames; i++)
				Frame();
		};

		~Driven()
		{
			ImGui::DestroyContext(context);

			std::error_code code;
			std::filesystem::remove(path, code);
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			editor.Draw(&open);
			ImGui::EndFrame();
		};

		// Presses at from, moves to to, and lets go.
		void Stroke(const ImVec2& from, const ImVec2& to)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddMousePosEvent(from.x, from.y);
			Frame();

			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();

			io.AddMousePosEvent(to.x, to.y);
			Frame();

			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		};

		void Press(ImGuiKey key)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(key, true);
			Frame();

			io.AddKeyEvent(key, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		};

		SpriteEditor::Image Saved()
		{
			Press(ImGuiKey_S);

			SpriteEditor::Image image;
			REQUIRE(SpriteEditor::Load(path, image));

			return image;
		};

		// The name of the tab in front, without its ### part.
		static std::string SelectedTab()
		{
			ImGuiContext& g = *ImGui::GetCurrentContext();

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					if (ImGuiTabItem* tab = ImGui::TabBarFindTabByID(bar, bar->SelectedTabId))
						return ImGui::TabBarGetTabName(bar, tab);

			return "";
		};

		std::string path;
		ImGuiContext* context = nullptr;
		SpriteEditor editor;
		bool open = true;
	};

	// A point in the canvas well inside the image, which sits in the middle of
	// it at many screen pixels to one of its own.
	const ImVec2 middle(Driven::width * 0.5f, Driven::height * 0.5f);

	// Farther either side of the middle than the image reaches, and still on
	// the canvas.
	constexpr float reach = 200.0f;
};


TEST_SUITE("SpriteEditor")
{
	TEST_CASE("a line reaches both ends with no gaps")
	{
		constexpr int length = 8;

		SpriteEditor::Image image = Blank(length, length);

		image.Line(0, 0, length - 1, 3, RED);

		CHECK(image.Get(0, 0) == RED);
		CHECK(image.Get(length - 1, 3) == RED);

		// One pixel a column, along the axis it covers more of.
		CHECK(Count(image, RED) == length);

		for (int x = 0; x < length; x++)
		{
			bool painted = false;

			for (int y = 0; y < length; y++)
				painted |= image.Get(x, y) == RED;

			CHECK(painted);
		};
	};

	TEST_CASE("a line off the image paints only the part on it")
	{
		SpriteEditor::Image image = Blank(4, 4);

		image.Line(-3, 1, 6, 1, RED);

		CHECK(Count(image, RED) == 4);
	};

	TEST_CASE("fill stops at other colours and does not leak through corners")
	{
		// A red wall down column 2, with a gap only at a corner.
		SpriteEditor::Image image = Blank(5, 3);

		image.Set(2, 0, RED);
		image.Set(2, 1, RED);
		image.Set(3, 2, RED);
		image.Set(2, 2, RED);

		image.Fill(0, 0, GREEN);

		CHECK(Count(image, GREEN) == 6);
		CHECK(image.Get(4, 0) == CLEAR);
		CHECK(Count(image, RED) == 4);
	};

	TEST_CASE("each format saves and loads back the same pixels, top row first")
	{
		SpriteEditor::Image image = Blank(3, 2);

		image.Set(0, 0, RED);
		image.Set(2, 1, GREEN);
		image.Set(1, 0, { 10, 20, 30, 128 });

		for (const char* extension : { ".png", ".bmp", ".tga" })
		{
			CAPTURE(extension);

			const std::string path = (std::filesystem::temp_directory_path() / (std::string("loom sprite round trip") + extension)).string();

			REQUIRE(SpriteEditor::Save(path, image));

			SpriteEditor::Image loaded;
			REQUIRE(SpriteEditor::Load(path, loaded));

			CHECK(loaded == image);

			std::filesystem::remove(path);
		};
	};

	TEST_CASE("a file that will not decode does not open")
	{
		SpriteEditor::Image image;

		CHECK_FALSE(SpriteEditor::Load("no such sprite.png", image));
	};

	TEST_CASE("a click with the pencil paints one pixel, and Ctrl+S writes it")
	{
		Driven driven("loom sprite click.tga");

		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("saving everything writes a painted sprite without Ctrl+S")
	{
		Driven driven("loom sprite save all.tga");

		driven.Stroke(middle, middle);

		CHECK(driven.editor.SaveAll());

		SpriteEditor::Image saved;
		REQUIRE(SpriteEditor::Load(driven.path, saved));

		CHECK(Count(saved, CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("a stroke across the image paints the row it crosses, end to end")
	{
		Driven driven("loom sprite stroke.tga");

		driven.Stroke(ImVec2(middle.x - reach, middle.y), ImVec2(middle.x + reach, middle.y));

		const SpriteEditor::Image saved = driven.Saved();

		CHECK(Count(saved, CLEAR) == Driven::side * (Driven::side - 1));
	};

	TEST_CASE("an image moved while its tab is in front stays in front, and Ctrl+S writes where it went")
	{
		Driven driven("loom sprite first.tga");

		const std::string opened = LoomTests::WriteTga("loom sprite opened.tga", Driven::side, Driven::side, Blank(Driven::side, Driven::side).pixels);
		const std::filesystem::path moved = std::filesystem::path(opened).parent_path() / "loom sprite moved.tga";

		driven.editor.Open(opened);
		driven.Frame();

		std::filesystem::rename(opened, moved);
		driven.editor.Relocate(std::filesystem::path(opened).lexically_normal().string(), moved.string());
		driven.Frame();

		CHECK(Driven::SelectedTab().find(moved.filename().string()) != std::string::npos);

		driven.Stroke(middle, middle);
		driven.Press(ImGuiKey_S);

		SpriteEditor::Image saved;
		REQUIRE(SpriteEditor::Load(moved.string(), saved));

		CHECK(Count(saved, CLEAR) == Driven::side * Driven::side - 1);
		CHECK_FALSE(std::filesystem::exists(opened));

		std::filesystem::remove(moved);
	};

	TEST_CASE("Ctrl+Z takes a stroke back and Ctrl+Y puts it back")
	{
		Driven driven("loom sprite undo.tga");

		driven.Stroke(middle, middle);
		driven.Press(ImGuiKey_Z);

		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side);

		driven.Press(ImGuiKey_Y);

		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("the eraser clears what the pencil painted")
	{
		Driven driven("loom sprite eraser.tga");

		driven.Stroke(middle, middle);

		ImGui::GetIO().AddKeyEvent(ImGuiKey_E, true);
		driven.Frame();
		ImGui::GetIO().AddKeyEvent(ImGuiKey_E, false);
		driven.Frame();

		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side);
	};

	TEST_CASE("a tool picked mid-stroke still lets the stroke end, and Ctrl+Z take it back")
	{
		Driven driven("loom sprite switch.tga");
		ImGuiIO& io = ImGui::GetIO();

		io.AddMousePosEvent(middle.x, middle.y);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();

		io.AddKeyEvent(ImGuiKey_G, true);
		driven.Frame();
		io.AddKeyEvent(ImGuiKey_G, false);
		driven.Frame();

		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		driven.Press(ImGuiKey_Z);

		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side);
	};

	TEST_CASE("fill paints the whole image when it is one colour")
	{
		Driven driven("loom sprite fill.tga");

		ImGui::GetIO().AddKeyEvent(ImGuiKey_G, true);
		driven.Frame();
		ImGui::GetIO().AddKeyEvent(ImGuiKey_G, false);
		driven.Frame();

		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), CLEAR) == 0);
	};
};

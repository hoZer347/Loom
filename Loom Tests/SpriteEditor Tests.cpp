#include "doctest.h"

#include "Test Support.h"

#include "SpriteEditor.h"
#include "UiScript.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>


namespace
{
	using Loom::SpriteAsset;
	using Loom::SpriteEditor;

	constexpr uint8_t FULL = 255;

	constexpr SpriteAsset::Colour RED = { FULL, 0, 0, FULL };
	constexpr SpriteAsset::Colour BLACK = { 0, 0, 0, FULL };
	constexpr SpriteAsset::Colour WHITE = { FULL, FULL, FULL, FULL };
	constexpr SpriteAsset::Colour CLEAR = { 0, 0, 0, 0 };

	int Count(const SpriteAsset::Image& image, const SpriteAsset::Colour& colour)
	{
		int count = 0;

		for (int y = 0; y < image.height; y++)
			for (int x = 0; x < image.width; x++)
				count += image.Get(x, y) == colour;

		return count;
	};

	// The Sprites panel in an ImGui context of the test's own, with no window
	// and no renderer, filling the display but for the Palette panel's room
	// on the right, with one small image open.
	struct Driven
	{
		static constexpr float width = 900.0f;
		static constexpr float height = 600.0f;

		// The image is fitted in the middle of the canvas, as large as it goes.
		static constexpr int side = 4;

		// The Palette panel's, which sits to the right of the Sprites panel.
		static constexpr float paletteWidth = 300.0f;

		// Enough for the panel to dock its tab and take focus.
		static constexpr int opening_frames = 4;

		static constexpr const char* paletteQuestion = "Open Palette?";

		// How the Palette panel starts: shut with the panel's question about it
		// answered Not Now, shut with the question left up, or open.
		enum struct PaletteStart
		{
			Declined,
			Asked,
			Open,
		};

		// With a sprite, it is written beside the image first, with the image
		// its sheet unless another is given, and the image opens as it.
		explicit Driven(const std::string& name, const SpriteAsset* sprite = nullptr, const SpriteAsset::Image* image = nullptr, PaletteStart palette = PaletteStart::Declined) :
			path(LoomTests::WriteTga(name, side, side, SpriteAsset::Image::Blank(side, side).pixels)),
			paletteOpen(palette == PaletteStart::Open)
		{
			if (sprite != nullptr)
			{
				REQUIRE(SpriteAsset::Save(SpriteAsset::For(path), *sprite, { path, "" }));
				REQUIRE(SpriteAsset::SaveImage(path, image != nullptr ? *image : sprite->Sheet()));
			};

			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width + paletteWidth, height);
			io.DeltaTime = frame_time;
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			editor.Open(path);

			for (int i = 0; i < opening_frames; i++)
				Frame();

			if (palette == PaletteStart::Declined)
				Run(std::string("click \"") + paletteQuestion + "\" \"Not Now\"\nwait 2\n");
		};

		~Driven()
		{
			ImGui::DestroyContext(context);

			std::error_code code;
			std::filesystem::remove(path, code);
			std::filesystem::remove(SpriteAsset::For(path), code);
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			editor.Draw(&open, &paletteOpen);

			// Beside the Sprites panel, once a test opens it.
			if (paletteOpen)
			{
				ImGui::SetNextWindowPos(ImVec2(width, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(paletteWidth, height));
				editor.DrawPalette(&paletteOpen, "");
			};
			ImGui::EndFrame();
		};

		// Presses at from, moves to to, and lets go.
		void Stroke(const ImVec2& from, const ImVec2& to, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddMousePosEvent(from.x, from.y);
			Frame();

			io.AddMouseButtonEvent(button, true);
			Frame();

			io.AddMousePosEvent(to.x, to.y);
			Frame();

			io.AddMouseButtonEvent(button, false);
			Frame();
		};

		// A key on its own, as a tool's is.
		void Tap(ImGuiKey key)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddKeyEvent(key, true);
			Frame();

			io.AddKeyEvent(key, false);
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

		// Runs a UiScript on the panels until it ends.
		void Run(const std::string& text)
		{
			constexpr int most_frames = 600;

			Loom::UiScript script{ std::filesystem::path() };
			script.LoadText(text);

			for (int frame = 0; frame < most_frames && !script.Finished(); frame++)
			{
				script.Step();
				Frame();
			};

			CHECK_FALSE(script.Failed());
		};

		// The sprite as Ctrl+S writes it.
		SpriteAsset SavedSprite()
		{
			Press(ImGuiKey_S);

			SpriteAsset sprite;
			SpriteAsset::Links links;
			REQUIRE(SpriteAsset::Load(SpriteAsset::For(path), sprite, links));

			return sprite;
		};

		// Whether the question about the Palette panel was up last frame.
		static bool Asking(const char* question = paletteQuestion)
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(question);

			return window != nullptr && window->Active;
		};

		// How many tabs the panel has.
		static int Tabs()
		{
			ImGuiContext& g = *ImGui::GetCurrentContext();
			int tabs = 0;

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					tabs += bar->Tabs.Size;

			return tabs;
		};

		SpriteAsset::Image Saved()
		{
			Press(ImGuiKey_S);

			SpriteAsset::Image image;
			REQUIRE(SpriteAsset::LoadImage(path, image));

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

		// Whether any tab has changes not yet saved.
		static bool Unsaved()
		{
			ImGuiContext& g = *ImGui::GetCurrentContext();

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					for (const ImGuiTabItem& tab : bar->Tabs)
						if (tab.Flags & ImGuiTabItemFlags_UnsavedDocument)
							return true;

			return false;
		};

		// The middle of the tab whose name holds name, which a script cannot
		// aim at by its label, or nowhere when there is none.
		static ImVec2 TabMiddle(const std::string& name)
		{
			ImGuiContext& g = *ImGui::GetCurrentContext();

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					for (ImGuiTabItem& tab : bar->Tabs)
						if (std::string(ImGui::TabBarGetTabName(bar, &tab)).find(name) != std::string::npos)
							return ImVec2(bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5f, (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f);

			return ImVec2();
		};

		std::string path;
		ImGuiContext* context = nullptr;
		SpriteEditor editor;
		bool open = true;
		bool paletteOpen;
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

		SpriteAsset::Image saved;
		REQUIRE(SpriteAsset::LoadImage(driven.path, saved));

		CHECK(Count(saved, CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("a stroke across the image paints the row it crosses, end to end")
	{
		Driven driven("loom sprite stroke.tga");

		driven.Stroke(ImVec2(middle.x - reach, middle.y), ImVec2(middle.x + reach, middle.y));

		const SpriteAsset::Image saved = driven.Saved();

		CHECK(Count(saved, CLEAR) == Driven::side * (Driven::side - 1));
	};

	TEST_CASE("an image moved while its tab is in front stays in front, and Ctrl+S writes it and its sprite where it went")
	{
		Driven driven("loom sprite first.tga");

		const std::string opened = LoomTests::WriteTga("loom sprite opened.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);
		const std::filesystem::path moved = std::filesystem::path(opened).parent_path() / "loom sprite moved.tga";

		driven.editor.Open(opened);
		driven.Frame();

		std::filesystem::rename(opened, moved);
		driven.editor.Relocate(std::filesystem::path(opened).lexically_normal().string(), moved.string());
		driven.Frame();

		CHECK(Driven::SelectedTab().find(moved.stem().string()) != std::string::npos);

		driven.Stroke(middle, middle);
		driven.Press(ImGuiKey_S);

		SpriteAsset::Image saved;
		REQUIRE(SpriteAsset::LoadImage(moved.string(), saved));

		CHECK(Count(saved, CLEAR) == Driven::side * Driven::side - 1);
		CHECK_FALSE(std::filesystem::exists(opened));
		CHECK(std::filesystem::exists(SpriteAsset::For(moved.string())));

		std::filesystem::remove(moved);
		std::filesystem::remove(SpriteAsset::For(moved.string()));
	};

	TEST_CASE("a stroke made before its image moved is taken back by Ctrl+Z without taking the move back")
	{
		Driven driven("loom sprite first.tga");

		const std::string opened = LoomTests::WriteTga("loom sprite unmoved.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);
		const std::filesystem::path moved = std::filesystem::path(opened).parent_path() / "loom sprite moved back.tga";

		driven.editor.Open(opened);
		driven.Frame();

		driven.Stroke(middle, middle);
		driven.Stroke(ImVec2(middle.x - reach, middle.y), ImVec2(middle.x + reach, middle.y));

		std::filesystem::rename(opened, moved);
		driven.editor.Relocate(std::filesystem::path(opened).lexically_normal().string(), moved.string());
		driven.Frame();

		driven.Press(ImGuiKey_Z);
		driven.Press(ImGuiKey_S);

		SpriteAsset::Image saved;
		REQUIRE(SpriteAsset::LoadImage(moved.string(), saved));

		CHECK(Count(saved, CLEAR) == Driven::side * Driven::side - 1);
		CHECK_FALSE(std::filesystem::exists(opened));
		CHECK_FALSE(std::filesystem::exists(SpriteAsset::For(opened)));

		std::filesystem::remove(moved);
		std::filesystem::remove(SpriteAsset::For(moved.string()));
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

	TEST_CASE("an image with a sprite opens as it: a stroke paints the first frame of the bottom layer, and saving writes both")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddFrame(1, false);
		sprite.AddLayer(1, "Hidden");
		sprite.layers[1].visible = false;
		sprite.layers[1].cels[0].Fill(0, 0, RED);

		Driven driven("loom sprite layered.tga", &sprite);

		driven.Stroke(middle, middle);

		// The hidden layer is left out, and the frames lie side by side.
		const SpriteAsset::Image sheet = driven.Saved();

		CHECK(sheet.width == Driven::side * 2);
		CHECK(Count(sheet, RED) == 0);
		CHECK(Count(sheet, CLEAR) == sheet.width * sheet.height - 1);

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));

		CHECK(Count(saved.layers[0].cels[0], CLEAR) == Driven::side * Driven::side - 1);
		CHECK(saved.layers[0].cels[1] == sprite.layers[0].cels[1]);
		CHECK(saved.layers[1] == sprite.layers[1]);
		CHECK(links.image == std::filesystem::path(driven.path).lexically_normal().string());
	};

	TEST_CASE("opening a sprite's image and then the sprite brings the one tab forward")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		Driven driven("loom sprite twice.tga", &sprite);

		driven.editor.Open(SpriteAsset::For(driven.path));
		driven.Frame();
		driven.Stroke(middle, middle);

		// Had a second tab opened, it would be the one in front, and painted.
		CHECK(Count(driven.Saved(), CLEAR) == Driven::side * Driven::side - 1);
		CHECK(Driven::Tabs() == 1);
	};

	TEST_CASE("a preview takes the place of the one before it, and leaves a kept tab open")
	{
		Driven driven("loom sprite preview kept.tga");

		const std::string first = LoomTests::WriteTga("loom sprite preview first.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);
		const std::string second = LoomTests::WriteTga("loom sprite preview second.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);

		driven.editor.Open(first, true);
		driven.Frame();
		driven.Frame();

		CHECK(Driven::Tabs() == 2);

		driven.editor.Open(second, true);
		driven.Frame();
		driven.Frame();

		CHECK(Driven::Tabs() == 2);
		CHECK(Driven::SelectedTab().find("loom sprite preview second") != std::string::npos);

		std::filesystem::remove(first);
		std::filesystem::remove(second);
	};

	TEST_CASE("a preview is kept by an edit, a double click on its tab, or opening it again to keep")
	{
		Driven driven("loom sprite preview edited.tga");

		std::vector<std::string> images;

		for (const char* name : { "loom sprite preview a.tga", "loom sprite preview b.tga", "loom sprite preview c.tga", "loom sprite preview d.tga" })
			images.push_back(LoomTests::WriteTga(name, Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels));

		const auto preview = [&driven](const std::string& image)
		{
			driven.editor.Open(image, true);
			driven.Frame();
			driven.Frame();
		};

		preview(images[0]);
		driven.Stroke(middle, middle);
		preview(images[1]);

		CHECK(Driven::Tabs() == 3);

		const ImVec2 tab = Driven::TabMiddle("loom sprite preview b");
		REQUIRE(tab.x > 0.0f);

		ImGuiIO& io = ImGui::GetIO();
		io.AddMousePosEvent(tab.x, tab.y);
		driven.Frame();

		for (int click = 0; click < 2; click++)
		{
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			driven.Frame();
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			driven.Frame();
		};

		preview(images[2]);

		CHECK(Driven::Tabs() == 4);

		driven.editor.Open(images[2]);
		preview(images[3]);

		CHECK(Driven::Tabs() == 5);

		for (const std::string& image : images)
		{
			std::filesystem::remove(image);
			std::filesystem::remove(SpriteAsset::For(image));
		};
	};

	TEST_CASE("a sprite with the Palette panel shut offers to open it, New there makes a palette of the sprite's colours written on save, and Ctrl+Z takes the choice back")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite palette.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n");

		CHECK(driven.paletteOpen);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		CHECK_FALSE(std::filesystem::exists(palette));

		SpriteAsset saved;
		SpriteAsset::Links links;

		driven.Press(ImGuiKey_Z);
		REQUIRE(driven.editor.SaveAll());
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette.empty());
		CHECK_FALSE(std::filesystem::exists(palette));

		driven.Press(ImGuiKey_Y);
		REQUIRE(driven.editor.SaveAll());
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette == palette);

		Loom::Palette made;
		REQUIRE(Loom::Palette::Load(palette, made));
		CHECK(made.colours == std::vector<SpriteAsset::Colour>{ RED });

		std::filesystem::remove(palette);
	};

	TEST_CASE("Clone makes a copy of the palette beside it and paints from that")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite palette clone.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\nclick \"Palette\" \"Clone\"\nwait 2\n");

		const std::filesystem::path palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension);
		const std::filesystem::path clone = palette.parent_path() / (palette.stem().string() + "1" + Loom::Palette::extension);

		REQUIRE(driven.editor.SaveAll());

		Loom::Palette cloned;
		REQUIRE(Loom::Palette::Load(clone.string(), cloned));
		CHECK(cloned.colours == std::vector<SpriteAsset::Colour>{ RED });

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette == clone.string());

		std::filesystem::remove(palette);
		std::filesystem::remove(clone);
	};

	TEST_CASE("a palette not yet saved is renamed where it waits, and saved under its new name")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite palette rename unsaved.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n");
		driven.Stroke(middle, middle);
		driven.Run("rightclick \"Palette\" \"New\" left -40 0\nwait 2\nkey ctrl+a\ntype Unsaved Hot\nkey enter\nwait 2\n");

		const std::filesystem::path folder = std::filesystem::path(driven.path).parent_path();
		const std::string hot = (folder / ("Unsaved Hot" + std::string(Loom::Palette::extension))).string();

		CHECK_FALSE(std::filesystem::exists(hot));
		CHECK(driven.editor.TakeRenamed().empty());

		// The stroke's undo step holds the link from before the rename.
		const ImVec2 tab = Driven::TabMiddle("rename unsaved");
		driven.Stroke(tab, tab);
		driven.Press(ImGuiKey_Z);

		REQUIRE(driven.editor.SaveAll());

		Loom::Palette saved_palette;
		REQUIRE(Loom::Palette::Load(hot, saved_palette));
		CHECK(saved_palette.colours == std::vector<SpriteAsset::Colour>{ RED });
		CHECK_FALSE(std::filesystem::exists(std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension)));

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette == hot);

		std::filesystem::remove(hot);
	};

	TEST_CASE("a palette renamed from its right-click menu takes the sprites painted from it along, open or saved, without marking them unsaved")
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "loom sprite palette rename";
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder / "Units");

		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite palette rename/Hero.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n");
		driven.Stroke(middle, middle);
		REQUIRE(driven.editor.SaveAll());

		const std::string palette = (folder / "Hero.loompalette").string();
		const std::string hot = (folder / "Hot.loompalette").string();
		const std::string other = (folder / "Units" / "Tank.loomsprite").string();
		REQUIRE(SpriteAsset::Save(other, sprite, { (folder / "Units" / "Tank.png").string(), palette }));

		driven.Run("rightclick \"Palette\" \"New\" left -40 0\nwait 2\nkey ctrl+a\ntype Hot\nkey enter\nwait 2\n");

		CHECK(std::filesystem::exists(hot));
		CHECK_FALSE(std::filesystem::exists(palette));
		CHECK_FALSE(Driven::Unsaved());
		CHECK(driven.editor.TakeRenamed() == std::vector<std::pair<std::string, std::string>>{ { palette, hot } });

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette == hot);
		REQUIRE(SpriteAsset::Load(other, saved, links));
		CHECK(links.palette == hot);

		// The stroke's undo step holds the link from before the rename.
		const ImVec2 tab = Driven::TabMiddle("Hero");
		driven.Stroke(tab, tab);
		driven.Press(ImGuiKey_Z);

		REQUIRE(driven.editor.SaveAll());
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));
		CHECK(links.palette == hot);
		CHECK_FALSE(std::filesystem::exists(palette));

		std::filesystem::remove_all(folder);
	};

	TEST_CASE("the panel opening with the Palette panel shut asks to open it, and Open Palette does")
	{
		Driven driven("loom sprite palette asked.tga", nullptr, nullptr, Driven::PaletteStart::Asked);

		CHECK(Driven::Asking());
		CHECK_FALSE(driven.paletteOpen);

		driven.Run(std::string("click \"") + Driven::paletteQuestion + "\" \"Open Palette\"\nwait 2\n");

		CHECK(driven.paletteOpen);
		CHECK_FALSE(Driven::Asking());
	};

	TEST_CASE("Not Now leaves the Palette panel shut and is not asked again while the panel stays open")
	{
		Driven driven("loom sprite palette declined.tga", nullptr, nullptr, Driven::PaletteStart::Asked);

		driven.Run(std::string("click \"") + Driven::paletteQuestion + "\" \"Not Now\"\nwait 2\n");

		for (int i = 0; i < Driven::opening_frames; i++)
			driven.Frame();

		CHECK_FALSE(driven.paletteOpen);
		CHECK_FALSE(Driven::Asking());
	};

	TEST_CASE("the panel opening with the Palette panel open asks nothing")
	{
		Driven driven("loom sprite palette open.tga", nullptr, nullptr, Driven::PaletteStart::Open);

		CHECK_FALSE(Driven::Asking());
		CHECK(driven.paletteOpen);
	};

	TEST_CASE("a palette made by New and moved before it is saved is written where it went")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite moved palette.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n");

		const std::filesystem::path palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension);
		const std::filesystem::path moved = palette.parent_path() / ("loom sprite moved" + std::string(Loom::Palette::extension));

		driven.editor.Relocate(palette.lexically_normal().string(), moved.lexically_normal().string());
		driven.Frame();
		REQUIRE(driven.editor.SaveAll());

		Loom::Palette made;
		REQUIRE(Loom::Palette::Load(moved.string(), made));
		CHECK(made.colours == std::vector<SpriteAsset::Colour>{ RED });
		CHECK_FALSE(std::filesystem::exists(palette));

		std::filesystem::remove(moved);
	};

	TEST_CASE("Resize scales the sprite, is written only on Ctrl+S, and Ctrl+Z and Ctrl+Y step it")
	{
		constexpr int wider = Driven::side * 2;

		Driven driven("loom sprite resize.tga");

		driven.Run(
			"click \"Sprites\" \"Resize\"\nwait 2\n"
			"click \"Popup\" \"##Width\" left\nkey ctrl+a\ntype " + std::to_string(wider) + "\nwait 2\n"
			"click \"Popup\" \"OK\"\nwait 2\n");

		SpriteAsset::Image image;
		REQUIRE(SpriteAsset::LoadImage(driven.path, image));
		CHECK(image.width == Driven::side);

		CHECK(driven.Saved().width == wider);

		driven.Press(ImGuiKey_Z);
		CHECK(driven.Saved().width == Driven::side);

		driven.Press(ImGuiKey_Y);
		CHECK(driven.Saved().width == wider);
	};

	TEST_CASE("+ Layer adds a clear layer above the one painted, a drag moves it, and - Layer takes it out")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		Driven driven("loom sprite layers.tga", &sprite);

		driven.Run("click \"Sprites\" \"+ Layer\"\nwait 2\ndrag \"Sprites\" \"##grip Layer 2\" to \"Sprites\" \"Layer 1\"\nwait 2\n");

		SpriteAsset saved = driven.SavedSprite();

		REQUIRE(saved.layers.size() == 2);
		CHECK(saved.layers[0].name == "Layer 2");
		CHECK(saved.layers[0].cels[0].IsClear());
		CHECK(saved.layers[1].name == "Layer 1");

		driven.Run("click \"Sprites\" \"- Layer\"\nwait 2\n");

		saved = driven.SavedSprite();

		REQUIRE(saved.layers.size() == 1);
		CHECK(saved.layers[0].name == "Layer 1");
	};

	TEST_CASE("- Frame on the last frame leaves the one before it to paint")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		Driven driven("loom sprite frames.tga", &sprite);

		driven.Run("click \"Sprites\" \"+ Frame\"\nwait 2\nclick \"Sprites\" \"- Frame\"\nwait 2\n");
		driven.Stroke(middle, middle);

		const SpriteAsset saved = driven.SavedSprite();

		REQUIRE(saved.Frames() == 1);
		CHECK(Count(saved.layers[0].cels[0], CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("a hidden layer is left out of the image, and Ctrl+Z shows it again")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite hidden.tga", &sprite);

		driven.Run("click \"Sprites\" \"##visible\"\nwait 2\n");

		CHECK(Count(driven.Saved(), RED) == 0);

		driven.Press(ImGuiKey_Z);

		CHECK(Count(driven.Saved(), RED) == 1);
	};

	TEST_CASE("a drag on a frame's milliseconds is one step to undo")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		Driven driven("loom sprite duration.tga", &sprite);

		driven.Run("drag \"Sprites\" \"##duration\" to \"Sprites\" \"##duration\" 40 0\nwait 2\n");

		CHECK(driven.SavedSprite().durations[0] > SpriteAsset::defaultDuration);

		driven.Press(ImGuiKey_Z);

		CHECK(driven.SavedSprite().durations[0] == SpriteAsset::defaultDuration);
	};

	TEST_CASE("Play steps the frames on by their milliseconds")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddFrame(1, false);

		// The first is over within a frame or two at sixty a second, and the
		// second outlasts the test.
		sprite.durations = { SpriteAsset::minDuration, SpriteAsset::maxDuration };

		Driven driven("loom sprite play.tga", &sprite);

		driven.Run("click \"Sprites\" \"Play\"\nwait 10\nclick \"Sprites\" \"Stop\"\nwait 2\n");
		driven.Stroke(middle, middle);

		const SpriteAsset saved = driven.SavedSprite();

		CHECK(saved.layers[0].cels[0].IsClear());
		CHECK(Count(saved.layers[0].cels[1], CLEAR) == Driven::side * Driven::side - 1);
	};

	TEST_CASE("an image written since its sprite is not saved over until it is taken in or the sprite kept")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		SpriteAsset::Image changed = SpriteAsset::Image::Blank(Driven::side, Driven::side);
		changed.Set(0, 0, RED);

		SUBCASE("taken in, it is a layer of its own")
		{
			Driven driven("loom sprite taken in.tga", &sprite, &changed);

			driven.Stroke(middle, middle);

			{
				const LoomTests::Captured captured(std::cerr);

				CHECK_FALSE(driven.editor.SaveAll());
				CHECK(captured.Says("loom sprite taken in.tga changed outside the sprite"));
			};

			driven.Run("click \"Sprites\" \"Take It In\"\nwait 2\n");

			const SpriteAsset saved = driven.SavedSprite();

			REQUIRE(saved.layers.size() == 2);
			CHECK(saved.layers[1].cels[0] == changed);
			CHECK(Count(driven.Saved(), RED) == 1);
		};

		SUBCASE("kept, the sprite is written over it")
		{
			Driven driven("loom sprite kept.tga", &sprite, &changed);

			driven.Run("click \"Sprites\" \"Keep Sprite\"\nwait 2\n");
			driven.Stroke(middle, middle);

			CHECK(driven.editor.SaveAll());
			CHECK(driven.SavedSprite().layers.size() == 1);
			CHECK(Count(driven.Saved(), RED) == 0);
		};
	};

	TEST_CASE("a lossy image saved from its sprite reads back as its own")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(1, 2, { 10, 200, 30, FULL / 2 });

		Driven driven("loom sprite lossy.jpg", &sprite);

		driven.Stroke(middle, middle);

		CHECK(driven.editor.SaveAll());
	};

	TEST_CASE("an image opens as the sprite beside it that names no image")
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path();
		const std::string image = (folder / "loom sprite bare.png").string();
		const std::string bare = SpriteAsset::For(image);

		REQUIRE(SpriteAsset::SaveImage(image, SpriteAsset::Image::Blank(Driven::side, Driven::side)));

		{
			std::ofstream file(bare);
			file << "size=" << Driven::side << "x" << Driven::side << "\nframe=100\nlayer=Bare\ncel=\n";
		};

		Driven driven("loom sprite beside bare.tga");

		driven.editor.Open(image);
		driven.Frame();
		driven.Frame();

		CHECK(Driven::Tabs() == 2);
		CHECK(Driven::SelectedTab().find("loom sprite bare") != std::string::npos);

		std::filesystem::remove(image);
		std::filesystem::remove(bare);
	};

	TEST_CASE("an image does not open as a sprite beside it that belongs to another image")
	{
		const SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		Driven driven("loom sprite owner.tga", &sprite);

		const std::string other = LoomTests::WriteTga("loom sprite other.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);
		REQUIRE(SpriteAsset::Save(SpriteAsset::For(other), sprite, { driven.path, "" }));

		const LoomTests::Captured captured(std::cerr);

		driven.editor.Open(other);
		driven.Frame();

		CHECK(Driven::Tabs() == 1);
		CHECK(captured.Says(SpriteAsset::For(other)));

		std::filesystem::remove(other);
		std::filesystem::remove(SpriteAsset::For(other));
	};

	TEST_CASE("the left mouse paints black and the right white, until X swaps them")
	{
		Driven driven("loom sprite hands.tga");

		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), WHITE) == 1);

		driven.Tap(ImGuiKey_X);
		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), WHITE) == 1);
		CHECK(Count(driven.Saved(), BLACK) == 0);

		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), BLACK) == 1);
	};

	TEST_CASE("the picker picks for the button that clicked it, and fill fills with that button's colour")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Fill(0, 0, RED);

		Driven driven("loom sprite picker.tga", &sprite);

		driven.Tap(ImGuiKey_I);
		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		// The picker goes back to the pencil, and the left colour is untouched.
		driven.Stroke(middle, middle);
		CHECK(Count(driven.Saved(), BLACK) == 1);

		driven.Tap(ImGuiKey_G);
		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), RED) == Driven::side * Driven::side);
	};

	TEST_CASE("the tool icons on the Palette panel's bar pick the fill, eraser and pencil as their keys do")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Fill(0, 0, RED);

		Driven driven("loom sprite tool icons.tga", &sprite);

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"🪣\"\nwait 2\n");
		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), WHITE) == Driven::side * Driven::side);

		driven.Run("click \"Palette\" \"🧽\"\nwait 2\n");
		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), WHITE) == Driven::side * Driven::side - 1);

		driven.Run("click \"Palette\" \"✏\"\nwait 2\n");
		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), BLACK) == 1);
	};

	TEST_CASE("a stroke ends when its own button lets go, whatever the other does")
	{
		Driven driven("loom sprite two buttons.tga");
		ImGuiIO& io = ImGui::GetIO();

		io.AddMousePosEvent(middle.x - reach, middle.y);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
		driven.Frame();

		io.AddMousePosEvent(middle.x + reach, middle.y);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		const SpriteAsset::Image saved = driven.Saved();

		CHECK(Count(saved, BLACK) == Driven::side);
		CHECK(Count(saved, WHITE) == 0);
	};

	TEST_CASE("a swatch clicked with either button is that button's colour, + and - put the left colour in the palette and take it out, + again even when it is there, written on Ctrl+S, and Ctrl+Z and Ctrl+Y step them")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite swatches.tga", &sprite);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		// The palette holds red alone, its swatch just left of +, after clear.
		const std::string swatch = "\"Palette\" \"+\" left -15 0";

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\nrightclick " + swatch + "\nwait 2\n");
		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), RED) == 2);

		driven.Run("click " + swatch + "\nwait 2\n");
		driven.Tap(ImGuiKey_E);
		driven.Stroke(middle, middle);
		driven.Tap(ImGuiKey_B);
		driven.Stroke(middle, middle);

		CHECK(Count(driven.Saved(), RED) == 2);

		Loom::Palette saved;

		driven.Run("click \"Palette\" \"-\"\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED });

		driven.Run("key ctrl+s\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours.empty());

		driven.Run("click \"Palette\" \"+\"\nwait 2\nkey ctrl+z\nwait 2\nkey ctrl+s\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours.empty());

		driven.Run("key ctrl+y\nwait 2\nkey ctrl+s\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED });

		driven.Run("click \"Palette\" \"+\"\nwait 2\nkey ctrl+s\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED, RED });

		std::filesystem::remove(palette);
	};

	TEST_CASE("clear comes first in every palette, paints as the eraser does, and is never added or taken out")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Fill(0, 0, RED);

		Driven driven("loom sprite clear swatch.tga", &sprite);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		// Clear, then red, then +.
		const std::string clear_swatch = "\"Palette\" \"+\" left -46 0";

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\nrightclick " + clear_swatch + "\nwait 2\n");
		driven.Stroke(middle, middle, ImGuiMouseButton_Right);

		CHECK(Count(driven.Saved(), RED) == Driven::side * Driven::side - 1);
		CHECK(Count(driven.Saved(), CLEAR) == 1);

		driven.Run("click " + clear_swatch + "\nwait 2\nclick \"Palette\" \"+\"\nwait 2\nclick \"Palette\" \"-\"\nwait 2\n");

		Loom::Palette saved;
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED });

		std::filesystem::remove(palette);
	};

	TEST_CASE("import puts every colour of the sprite in a category named after it, those the palette has already included, written on Ctrl+S and stepped by Ctrl+Z and Ctrl+Y")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);
		sprite.layers[0].cels[0].Set(1, 0, WHITE);

		Driven driven("loom sprite import.tga", &sprite);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\nclick \"Palette\" \"📥\"\nwait 2\n");

		CHECK_FALSE(std::filesystem::exists(palette));

		const auto saved_after = [&driven, &palette](const std::string& keys)
		{
			driven.Run(keys + "key ctrl+s\nwait 2\n");

			Loom::Palette saved;
			REQUIRE(Loom::Palette::Load(palette, saved));

			return saved;
		};

		using Categories = std::vector<Loom::Palette::Category>;
		const Categories imported = { { "loom sprite import", { RED, WHITE } } };

		const Loom::Palette saved = saved_after("");
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED, WHITE });
		CHECK(saved.categories == imported);

		CHECK(saved_after("key ctrl+z\nwait 2\n").categories.empty());
		CHECK(saved_after("key ctrl+y\nwait 2\n").categories == imported);

		std::filesystem::remove(palette);
	};

	TEST_CASE("import takes 128 colours straight in, asks first for more, and Cancel leaves the palette as it was")
	{
		constexpr int many_side = 16;
		constexpr int most_unasked = 128;
		constexpr const char* import_question = "Import Colours?";

		// The top half holds the colours, leaving the middle clear for the pencil to add one more.
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(many_side, many_side));

		for (int i = 0; i < most_unasked; i++)
			sprite.layers[0].cels[0].Set(i % many_side, i / many_side, SpriteAsset::Colour{ (uint8_t)(i + 1), 0, 0, FULL });

		Driven driven("loom sprite many colours.tga", &sprite);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		const auto saved_categories = [&driven, &palette](const std::string& steps)
		{
			driven.Run(steps + "key ctrl+s\nwait 2\n");

			Loom::Palette saved;
			REQUIRE(Loom::Palette::Load(palette, saved));

			return saved.categories;
		};

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\nclick \"Palette\" \"📥\"\nwait 2\n");
		CHECK_FALSE(Driven::Asking(import_question));

		std::vector<Loom::Palette::Category> categories = saved_categories("");
		REQUIRE(categories.size() == 1);
		CHECK(categories[0].colours.size() == most_unasked);

		driven.Stroke(middle, middle);
		driven.Run("click \"Palette\" \"📥\"\nwait 2\n");
		CHECK(Driven::Asking(import_question));

		driven.Run(std::string("click \"") + import_question + "\" \"Cancel\"\nwait 2\n");
		CHECK_FALSE(Driven::Asking(import_question));
		CHECK(saved_categories("").size() == 1);

		categories = saved_categories(std::string("click \"Palette\" \"📥\"\nwait 2\nclick \"") + import_question + "\" \"Import\"\nwait 2\n");
		REQUIRE(categories.size() == 2);
		CHECK(categories[1].colours.size() == most_unasked + 1);

		CHECK(saved_categories("key ctrl+z\nwait 2\n").size() == 1);

		std::filesystem::remove(palette);
	};

	TEST_CASE("a see-through colour with red, green or blue left in it is clear, paints as clear, and + leaves it out of the palette")
	{
		constexpr SpriteAsset::Colour see_through_red = { FULL, 0, 0, 0 };

		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Fill(0, 0, see_through_red);

		// A cel that is all see-through is saved as clear, so one pixel shows.
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite see-through.tga", &sprite);

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		driven.Run("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n");
		driven.Tap(ImGuiKey_I);
		driven.Stroke(middle, middle);
		driven.Stroke(middle, middle);

		CHECK(Count(driven.SavedSprite().layers[0].cels[0], CLEAR) == 1);

		driven.Run("click \"Palette\" \"+\"\nwait 2\n");

		Loom::Palette saved;
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED });

		std::filesystem::remove(palette);
	};

	// Below the swatches, where no item is.
	constexpr const char* palette_space = "1050 550";

	TEST_CASE("right-clicking the Palette panel adds a category, named as it is added, whose + adds the left colour under it, written on Ctrl+S")
	{
		Driven driven("loom sprite category.tga");

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		driven.Run(std::string("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n")
			+ "rightclick " + palette_space + "\nwait 2\nclick \"\" \"New Category\"\nwait 2\n"
			+ "key ctrl+a\ntype Darks\nkey enter\nwait 2\nclick \"Palette\" \"+##Darks\"\nwait 2\n");

		CHECK_FALSE(std::filesystem::exists(palette));

		driven.Run("key ctrl+s\nwait 2\n");

		Loom::Palette saved;
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.colours.empty());
		CHECK(saved.categories == std::vector<Loom::Palette::Category>{ { "Darks", { BLACK } } });

		driven.Run("rightclick \"Palette\" \"Darks\"\nwait 2\nclick \"\" \"Delete Category\"\nwait 2\nkey ctrl+s\nwait 2\n");
		REQUIRE(Loom::Palette::Load(palette, saved));
		CHECK(saved.categories.empty());

		std::filesystem::remove(palette);
	};

	TEST_CASE("Ctrl+Z in the Palette panel takes back a category's colour, its name and the category itself, and Ctrl+Y puts them back")
	{
		Driven driven("loom sprite category undo.tga");

		const std::string palette = std::filesystem::path(driven.path).replace_extension(Loom::Palette::extension).string();

		driven.Run(std::string("click \"Sprites\" \"Open Palette\"\nwait 2\nclick \"Palette\" \"New\"\nwait 2\n")
			+ "rightclick " + palette_space + "\nwait 2\nclick \"\" \"New Category\"\nwait 2\n"
			+ "key ctrl+a\ntype Darks\nkey enter\nwait 2\nclick \"Palette\" \"+##Darks\"\nwait 2\n"
			+ "rightclick \"Palette\" \"Darks\"\nwait 2\nclick \"\" \"Delete Category\"\nwait 2\n");

		const auto saved_after = [&driven, &palette](const std::string& keys)
		{
			driven.Run(keys + "\nwait 2\nkey ctrl+s\nwait 2\n");

			Loom::Palette saved;
			REQUIRE(Loom::Palette::Load(palette, saved));

			return saved.categories;
		};

		using Categories = std::vector<Loom::Palette::Category>;

		CHECK(saved_after("key ctrl+z") == Categories{ { "Darks", { BLACK } } });
		CHECK(saved_after("key ctrl+z") == Categories{ { "Darks", { } } });
		CHECK(saved_after("key ctrl+z") == Categories{ { "Category", { } } });
		CHECK(saved_after("key ctrl+z").empty());
		CHECK(saved_after("key ctrl+y") == Categories{ { "Category", { } } });

		std::filesystem::remove(palette);
	};

	TEST_CASE("right-clicking the Palette panel opens another palette from the list of them")
	{
		Driven driven("loom sprite open palette.tga");

		const std::string other = std::filesystem::path(driven.path).replace_filename("loom other.loompalette").lexically_normal().string();
		REQUIRE(Loom::Palette::Save(other, Loom::Palette{ { RED } }));

		driven.Run(std::string("click \"Sprites\" \"Open Palette\"\nwait 2\n")
			+ "rightclick " + palette_space + "\nwait 2\nclick \"\" \"Open Palette\"\nwait 2\nclick \"\" \"loom other.loompalette\"\nwait 2\n"
			+ "click \"Palette\" \"+\"\nwait 2\nkey ctrl+s\nwait 2\n");

		Loom::Palette saved;
		REQUIRE(Loom::Palette::Load(other, saved));
		CHECK(saved.colours == std::vector<SpriteAsset::Colour>{ RED, BLACK });

		std::filesystem::remove(other);
	};

	TEST_CASE("a layer's outline toggle is kept with the sprite, changes nothing in the image, and Ctrl+Z turns it back")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.layers[0].cels[0].Set(0, 0, RED);

		Driven driven("loom sprite outlined.tga", &sprite);

		driven.Run("click \"Sprites\" \"##outlined\"\nwait 2\n");

		CHECK(driven.SavedSprite().layers[0].outlined);
		CHECK(driven.Saved() == sprite.Sheet());

		driven.Press(ImGuiKey_Z);

		CHECK_FALSE(driven.SavedSprite().layers[0].outlined);
	};

	TEST_CASE("a layer dragged by its grip past others takes their place, is the one painted, and Ctrl+Z puts it back in one step")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddLayer(1, "Layer 2");
		sprite.AddLayer(2, "Layer 3");

		Driven driven("loom sprite drag layer.tga", &sprite);

		driven.Run("drag \"Sprites\" \"##grip Layer 1\" to \"Sprites\" \"Layer 3\"\nwait 2\n");
		driven.Stroke(middle, middle);

		SpriteAsset saved = driven.SavedSprite();

		REQUIRE(saved.layers.size() == 3);
		CHECK(saved.layers[0].name == "Layer 2");
		CHECK(saved.layers[1].name == "Layer 3");
		CHECK(saved.layers[2].name == "Layer 1");
		CHECK(Count(saved.layers[2].cels[0], CLEAR) == Driven::side * Driven::side - 1);

		driven.Press(ImGuiKey_Z);
		driven.Press(ImGuiKey_Z);

		saved = driven.SavedSprite();

		CHECK(saved.layers[0].name == "Layer 1");
		CHECK(saved.layers[2].name == "Layer 3");
	};

	TEST_CASE("a layer dragged by its name stays where it is")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddLayer(1, "Layer 2");

		Driven driven("loom sprite drag name.tga", &sprite);

		driven.Run("drag \"Sprites\" \"Layer 1\" to \"Sprites\" \"Layer 2\"\nwait 2\n");
		driven.Stroke(middle, middle);

		const SpriteAsset saved = driven.SavedSprite();

		REQUIRE(saved.layers.size() == 2);
		CHECK(saved.layers[0].name == "Layer 1");
		CHECK(saved.layers[1].name == "Layer 2");
	};

	TEST_CASE("a layer held over another sprite's tab leaves that tab behind")
	{
		constexpr int held_frames = 60;

		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddLayer(1, "Layer 2");

		Driven driven("loom sprite drag first.tga", &sprite);

		const std::string other = LoomTests::WriteTga("loom sprite drag other.tga", Driven::side, Driven::side, SpriteAsset::Image::Blank(Driven::side, Driven::side).pixels);

		driven.editor.Open(other);
		driven.Frame();
		driven.Frame();
		driven.editor.Open(driven.path);
		driven.Frame();
		driven.Frame();

		const std::string first = Driven::SelectedTab();
		const ImVec2 other_tab = Driven::TabMiddle(std::filesystem::path(SpriteAsset::For(other)).filename().string());

		REQUIRE(other_tab.x > 0.0f);

		ImGuiIO& io = ImGui::GetIO();

		driven.Run("move \"Sprites\" \"##grip Layer 1\"\nwait 2\n");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();
		driven.Run("move " + std::to_string((int)other_tab.x) + " " + std::to_string((int)other_tab.y) + "\nwait " + std::to_string(held_frames) + "\n");

		CHECK(Driven::SelectedTab() == first);

		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		CHECK(Driven::SelectedTab() == first);

		std::filesystem::remove(other);
	};

	TEST_CASE("Ctrl+Z waits while a layer is held, so it lets go of the layer it picked up")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));
		sprite.AddLayer(1, "Layer 2");

		Driven driven("loom sprite undo mid-drag.tga", &sprite);
		ImGuiIO& io = ImGui::GetIO();

		// A third layer, between the two, for Ctrl+Z to take out.
		driven.Run("click \"Sprites\" \"+ Layer\"\nwait 2\nmove \"Sprites\" \"##grip Layer 2\"\nwait 2\n");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();
		driven.Run("move \"Sprites\" \"Layer 1\"\nwait 2\n");
		driven.Press(ImGuiKey_Z);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		// Saved as a whole, wherever the keyboard is.
		REQUIRE(driven.editor.SaveAll());

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));

		REQUIRE(saved.layers.size() == 3);
		CHECK(saved.layers[0].name == "Layer 2");
		CHECK(saved.layers[1].name == "Layer 1");
		CHECK(saved.layers[2].name == "Layer 3");
	};

	TEST_CASE("a layer held above the timeline's rows climbs past those out of sight, the rows scrolling with it")
	{
		constexpr int layers = 6;
		constexpr int held_frames = 120;
		constexpr int scroll_to_bottom = -10;

		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(Driven::side, Driven::side));

		for (int layer = 1; layer < layers; layer++)
			sprite.AddLayer(layer, "Layer " + std::to_string(layer + 1));

		Driven driven("loom sprite scrolled layers.tga", &sprite);
		ImGuiIO& io = ImGui::GetIO();

		// The bottom layer comes into sight at the foot of the rows, and the
		// mouse holds it up by the buttons above them.
		driven.Run("move \"Sprites\" \"Layer 4\"\nscroll " + std::to_string(scroll_to_bottom) + "\nwait 2\nmove \"Sprites\" \"##grip Layer 1\"\nwait 2\n");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();
		driven.Run("move \"Sprites\" \"+ Layer\"\nwait " + std::to_string(held_frames) + "\n");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		REQUIRE(driven.editor.SaveAll());

		SpriteAsset saved;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(SpriteAsset::For(driven.path), saved, links));

		REQUIRE(saved.layers.size() == layers);
		CHECK(saved.layers.back().name == "Layer 1");
		CHECK(saved.layers.front().name == "Layer 2");
	};
};

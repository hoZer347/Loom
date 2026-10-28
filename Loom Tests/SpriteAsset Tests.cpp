#include "doctest.h"

#include "Test Support.h"

#include "SpriteAsset.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>


#pragma region Helpers

namespace
{
	using Loom::Palette;
	using Loom::SpriteAsset;

	constexpr uint8_t FULL = 255;
	constexpr uint8_t HALF = 128;

	constexpr SpriteAsset::Colour RED = { FULL, 0, 0, FULL };
	constexpr SpriteAsset::Colour GREEN = { 0, FULL, 0, FULL };
	constexpr SpriteAsset::Colour BLUE = { 0, 0, FULL, FULL };
	constexpr SpriteAsset::Colour CLEAR = { 0, 0, 0, 0 };

	int Count(const SpriteAsset::Image& image, const SpriteAsset::Colour& colour)
	{
		int count = 0;

		for (int y = 0; y < image.height; y++)
			for (int x = 0; x < image.width; x++)
				count += image.Get(x, y) == colour;

		return count;
	};

	std::filesystem::path Temp(const std::string& name)
	{
		return std::filesystem::temp_directory_path() / name;
	};

	// Two frames of two layers, the top one hidden, with a palette beside it.
	SpriteAsset TwoByTwo()
	{
		constexpr int side = 3;

		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(side, side));
		sprite.AddFrame(1, false);
		sprite.AddLayer(1, "Top");
		sprite.durations[1] = SpriteAsset::defaultDuration * 2;

		sprite.layers[0].cels[0].Set(0, 0, RED);
		sprite.layers[0].cels[1].Set(1, 1, GREEN);
		sprite.layers[1].cels[1].Set(2, 2, BLUE);
		sprite.layers[1].visible = false;
		sprite.layers[0].outlined = true;

		return sprite;
	};
};

#pragma endregion


TEST_SUITE("SpriteAsset")
{
	#pragma region Drawing

	TEST_CASE("a line reaches both ends with no gaps")
	{
		constexpr int length = 8;

		SpriteAsset::Image image = SpriteAsset::Image::Blank(length, length);

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
		SpriteAsset::Image image = SpriteAsset::Image::Blank(4, 4);

		image.Line(-3, 1, 6, 1, RED);

		CHECK(Count(image, RED) == 4);
	};

	TEST_CASE("fill stops at other colours and does not leak through corners")
	{
		// A red wall down column 2, with a gap only at a corner.
		SpriteAsset::Image image = SpriteAsset::Image::Blank(5, 3);

		image.Set(2, 0, RED);
		image.Set(2, 1, RED);
		image.Set(3, 2, RED);
		image.Set(2, 2, RED);

		image.Fill(0, 0, GREEN);

		CHECK(Count(image, GREEN) == 6);
		CHECK(image.Get(4, 0) == CLEAR);
		CHECK(Count(image, RED) == 4);
	};

	#pragma endregion

	#pragma region Image Files

	TEST_CASE("each format saves and loads back the same pixels, top row first")
	{
		SpriteAsset::Image image = SpriteAsset::Image::Blank(3, 2);

		image.Set(0, 0, RED);
		image.Set(2, 1, GREEN);
		image.Set(1, 0, { 10, 20, 30, HALF });

		for (const char* extension : { ".png", ".bmp", ".tga" })
		{
			CAPTURE(extension);

			const std::string path = Temp(std::string("loom sprite round trip") + extension).string();

			REQUIRE(SpriteAsset::SaveImage(path, image));

			SpriteAsset::Image loaded;
			REQUIRE(SpriteAsset::LoadImage(path, loaded));

			CHECK(loaded == image);

			std::filesystem::remove(path);
		};
	};

	TEST_CASE("a file that will not decode does not open")
	{
		const LoomTests::Captured captured(std::cerr);
		SpriteAsset::Image image;

		CHECK_FALSE(SpriteAsset::LoadImage("no such sprite.png", image));
		CHECK(captured.Says("no such sprite.png"));
	};

	#pragma endregion

	#pragma region Blending

	TEST_CASE("an opaque pixel covers what is under it, and a see-through one blends with it")
	{
		SpriteAsset::Image bottom = SpriteAsset::Image::Blank(3, 1);
		bottom.Set(0, 0, RED);
		bottom.Set(1, 0, RED);

		SpriteAsset::Image top = SpriteAsset::Image::Blank(3, 1);
		top.Set(0, 0, BLUE);
		top.Set(1, 0, { 0, 0, FULL, HALF });
		top.Set(2, 0, GREEN);

		bottom.Over(top);

		CHECK(bottom.Get(0, 0) == BLUE);
		CHECK(bottom.Get(1, 0) == SpriteAsset::Colour{ FULL - HALF, 0, HALF, FULL });
		CHECK(bottom.Get(2, 0) == GREEN);
	};

	#pragma endregion

	#pragma region Sprite Files

	TEST_CASE("a sprite saves and loads back its frames, layers and links")
	{
		const std::filesystem::path folder = Temp("loom sprite asset");
		std::filesystem::create_directories(folder / "Palettes");

		const std::string path = (folder / "Walk.loomsprite").string();
		const SpriteAsset::Links links{ (folder / "Walk.png").string(), (folder / "Palettes" / "Walk.loompalette").string() };
		const SpriteAsset sprite = TwoByTwo();

		REQUIRE(SpriteAsset::Save(path, sprite, links));

		SpriteAsset loaded;
		SpriteAsset::Links loaded_links;
		REQUIRE(SpriteAsset::Load(path, loaded, loaded_links));

		CHECK(loaded == sprite);
		CHECK(loaded_links == links);

		// Relative, so the folder can move whole.
		std::ifstream file(path);
		std::string first;
		std::getline(file, first);
		CHECK(first == "image=Walk.png");

		file.close();
		std::filesystem::remove_all(folder);
	};

	TEST_CASE("a sprite missing a cel does not load")
	{
		const std::string path = Temp("loom sprite broken.loomsprite").string();

		{
			std::ofstream file(path);
			file << "image=broken.png\nsize=2x2\nframe=100\nframe=100\nlayer=Only\ncel=\n";
		};

		const LoomTests::Captured captured(std::cerr);
		SpriteAsset sprite;
		SpriteAsset::Links links;
		CHECK_FALSE(SpriteAsset::Load(path, sprite, links));
		CHECK(captured.Says(path));

		std::filesystem::remove(path);
	};

	TEST_CASE("a sprite of no size, too wide, or with more cels than frames does not load")
	{
		const std::string path = Temp("loom sprite sized.loomsprite").string();

		const std::string sprites[] =
		{
			"size=-2x3\nframe=100\nlayer=Only\ncel=\n",
			"size=" + std::to_string(SpriteAsset::maxSide + 1) + "x1\nframe=100\nlayer=Only\ncel=\n",
			"size=2x2\nframe=100\nlayer=Only\ncel=\ncel=\n",
		};

		for (const std::string& text : sprites)
		{
			{
				std::ofstream file(path);
				file << text;
			};

			const LoomTests::Captured captured(std::cerr);
			SpriteAsset sprite;
			SpriteAsset::Links links;

			CHECK_NOTHROW(CHECK_FALSE(SpriteAsset::Load(path, sprite, links)));
			CHECK(captured.Says(path + " is not a whole sprite"));
		};

		std::filesystem::remove(path);
	};

	TEST_CASE("a frame's milliseconds are held to what a frame may last")
	{
		const std::string path = Temp("loom sprite durations.loomsprite").string();

		{
			std::ofstream file(path);
			file << "size=1x1\nframe=0\nframe=-5\nframe=99999\nlayer=Only\ncel=\ncel=\ncel=\n";
		};

		SpriteAsset sprite;
		SpriteAsset::Links links;
		REQUIRE(SpriteAsset::Load(path, sprite, links));

		CHECK(sprite.durations == std::vector<int>{ SpriteAsset::minDuration, SpriteAsset::minDuration, SpriteAsset::maxDuration });

		std::filesystem::remove(path);
	};

	TEST_CASE("a sprite of more frames than the timeline holds does not load")
	{
		const std::string path = Temp("loom sprite long.loomsprite").string();

		{
			std::ofstream file(path);
			file << "size=1x1\n";

			for (int frame = 0; frame <= SpriteAsset::maxFrames; frame++)
				file << "frame=100\n";

			file << "layer=Only\n";

			for (int frame = 0; frame <= SpriteAsset::maxFrames; frame++)
				file << "cel=\n";
		};

		const LoomTests::Captured captured(std::cerr);
		SpriteAsset sprite;
		SpriteAsset::Links links;
		CHECK_FALSE(SpriteAsset::Load(path, sprite, links));
		CHECK(captured.Says(path));

		std::filesystem::remove(path);
	};

	#pragma endregion

	#pragma region Sheets and Frames

	TEST_CASE("an image the size of the sheet is taken in as a layer cut into the frames, and any other starts the sprite over")
	{
		SpriteAsset sprite = TwoByTwo();
		SpriteAsset::Image sheet = SpriteAsset::Image::Blank(sprite.width * 2, sprite.height);
		sheet.Set(sprite.width, 0, BLUE);

		sprite.TakeIn(sheet, "Taken");

		REQUIRE(sprite.layers.size() == 3);
		CHECK(sprite.layers[2].name == "Taken");
		CHECK(sprite.layers[2].cels[0].IsClear());
		CHECK(sprite.layers[2].cels[1].Get(0, 0) == BLUE);

		sprite.TakeIn(SpriteAsset::Image::Blank(1, 1), "Small");

		CHECK(sprite == SpriteAsset::FromImage(SpriteAsset::Image::Blank(1, 1)));
	};

	TEST_CASE("the sheet lays the frames side by side, without the hidden layers")
	{
		const SpriteAsset sprite = TwoByTwo();
		const SpriteAsset::Image sheet = sprite.Sheet();

		CHECK(sheet.width == sprite.width * 2);
		CHECK(sheet.height == sprite.height);
		CHECK(sheet.Get(0, 0) == RED);
		CHECK(sheet.Get(sprite.width + 1, 1) == GREEN);
		CHECK(Count(sheet, BLUE) == 0);
		CHECK(Count(sheet, CLEAR) == sheet.width * sheet.height - 2);
	};

	TEST_CASE("a copied frame holds what the one before it did, and a new one is clear")
	{
		SpriteAsset sprite = TwoByTwo();

		sprite.AddFrame(1, true);
		sprite.AddFrame(0, true);

		REQUIRE(sprite.Frames() == 4);
		CHECK(sprite.Flatten(0) == SpriteAsset::Image::Blank(sprite.width, sprite.height));
		CHECK(sprite.Flatten(2) == sprite.Flatten(1));
		CHECK(sprite.durations[2] == sprite.durations[1]);

		sprite.RemoveFrame(0);

		CHECK(sprite.Frames() == 3);
		CHECK(sprite.Flatten(0).Get(0, 0) == RED);
	};

	TEST_CASE("a layer moved up or down takes the place it is moved to, and those between make room")
	{
		SpriteAsset sprite = SpriteAsset::FromImage(SpriteAsset::Image::Blank(1, 1));
		sprite.AddLayer(1, "Layer 2");
		sprite.AddLayer(2, "Layer 3");

		const auto names = [&sprite]()
		{
			std::vector<std::string> names;

			for (const SpriteAsset::Layer& layer : sprite.layers)
				names.push_back(layer.name);

			return names;
		};

		sprite.MoveLayer(0, 2);
		CHECK(names() == std::vector<std::string>{ "Layer 2", "Layer 3", "Layer 1" });

		sprite.MoveLayer(2, 0);
		CHECK(names() == std::vector<std::string>{ "Layer 1", "Layer 2", "Layer 3" });

		sprite.MoveLayer(1, 1);
		CHECK(names() == std::vector<std::string>{ "Layer 1", "Layer 2", "Layer 3" });
	};

	TEST_CASE("a resized sprite scales every cel to the size, or cuts and grows it clear about the anchor")
	{
		constexpr int scale = 2;

		SpriteAsset scaled = TwoByTwo();
		scaled.Resize(scaled.width * scale, scaled.height * scale, true, 0.0f, 0.0f);

		CHECK(scaled.width == TwoByTwo().width * scale);
		CHECK(scaled.durations == TwoByTwo().durations);
		CHECK(Count(scaled.layers[0].cels[0], RED) == scale * scale);
		CHECK(scaled.layers[0].cels[1].Get(3, 3) == GREEN);
		CHECK(scaled.layers[1].cels[1].Get(5, 5) == BLUE);

		SpriteAsset grown = TwoByTwo();
		grown.Resize(grown.width + 2, grown.height + 2, false, 0.5f, 0.5f);

		CHECK(grown.layers[0].cels[0].Get(1, 1) == RED);
		CHECK(grown.layers[0].cels[0].Get(0, 0) == CLEAR);
		CHECK(grown.layers[0].cels[1].Get(2, 2) == GREEN);

		SpriteAsset cut = TwoByTwo();
		cut.Resize(1, 1, false, 1.0f, 1.0f);

		CHECK(cut.layers[0].cels[0].Get(0, 0) == CLEAR);
		CHECK(cut.layers[1].cels[1].Get(0, 0) == BLUE);
	};

	TEST_CASE("a sprite's colours are its visible ones, each once")
	{
		const std::vector<SpriteAsset::Colour> colours = TwoByTwo().Colours(SIZE_MAX);

		CHECK(colours == std::vector<SpriteAsset::Colour>{ RED, GREEN });
		CHECK(TwoByTwo().Colours(1).size() == 1);
	};

	#pragma endregion

	#pragma region Linked Images

	TEST_CASE("an image and its sprite find each other, and a sprite naming another image is not its")
	{
		const std::filesystem::path folder = Temp("loom sprite companions");
		std::filesystem::create_directories(folder);

		const std::string image = (folder / "Tank.png").string();
		const std::string sprite = SpriteAsset::For(image);
		const std::string other = (folder / "Other.png").string();

		REQUIRE(SpriteAsset::SaveImage(image, SpriteAsset::Image::Blank(1, 1)));
		REQUIRE(SpriteAsset::SaveImage(other, SpriteAsset::Image::Blank(1, 1)));

		const SpriteAsset blank = SpriteAsset::FromImage(SpriteAsset::Image::Blank(1, 1));

		REQUIRE(SpriteAsset::Save(sprite, blank, { image, "" }));
		CHECK(SpriteAsset::Companion(image) == sprite);
		CHECK(SpriteAsset::Companion(sprite) == image);

		REQUIRE(SpriteAsset::Save(sprite, blank, { other, "" }));
		CHECK(SpriteAsset::Companion(image).empty());
		CHECK(SpriteAsset::Companion(sprite) == other);

		std::filesystem::remove_all(folder);
	};

	TEST_CASE("a sprite naming no image is the .png beside it, which finds it in turn")
	{
		const std::filesystem::path folder = Temp("loom sprite bare companions");
		std::filesystem::create_directories(folder);

		const std::string image = (folder / "Bare.png").string();
		const std::string sprite = SpriteAsset::For(image);

		REQUIRE(SpriteAsset::SaveImage(image, SpriteAsset::Image::Blank(1, 1)));

		{
			std::ofstream file(sprite);
			file << "size=1x1\nframe=100\nlayer=Only\ncel=\n";
		};

		CHECK(SpriteAsset::ImageOf(sprite) == image);
		CHECK(SpriteAsset::Companion(sprite) == image);
		CHECK(SpriteAsset::Companion(image) == sprite);

		std::filesystem::remove_all(folder);
	};

	TEST_CASE("a lossy format's image as saved is what reading it back gives")
	{
		SpriteAsset::Image image = SpriteAsset::Image::Blank(2, 2);
		image.Set(0, 0, { 10, 200, 30, HALF });

		const std::string path = Temp("loom sprite as saved.jpg").string();
		REQUIRE(SpriteAsset::SaveImage(path, image));

		SpriteAsset::Image loaded;
		REQUIRE(SpriteAsset::LoadImage(path, loaded));

		CHECK(loaded != image);
		CHECK(loaded == SpriteAsset::AsSaved(path, image));
		CHECK(SpriteAsset::AsSaved("Lossless.png", image) == image);

		std::filesystem::remove(path);
	};

	#pragma endregion

	#pragma region Palettes

	TEST_CASE("a palette saves and loads back its colours, reading #RRGGBB as opaque and skipping the rest")
	{
		const std::string path = Temp("loom palette.loompalette").string();
		const Palette palette{ { RED, { 1, 2, 3, HALF } } };

		REQUIRE(Palette::Save(path, palette));

		Palette loaded;
		REQUIRE(Palette::Load(path, loaded));
		CHECK(loaded == palette);

		{
			std::ofstream file(path);
			file << "#00FF00\nnot a colour\n#12345\n#0000FFFF\n";
		};

		REQUIRE(Palette::Load(path, loaded));
		CHECK(loaded.colours == std::vector<SpriteAsset::Colour>{ GREEN, BLUE });

		std::filesystem::remove(path);
	};

	TEST_CASE("a palette's categories save and load back with the colours under each, apart from its own")
	{
		const std::string path = Temp("loom palette categories.loompalette").string();
		const Palette palette{ { RED }, { { "Greens", { GREEN } }, { "Empty", { } }, { "Blues", { BLUE, { 1, 2, 3, HALF } } } } };

		REQUIRE(Palette::Save(path, palette));

		Palette loaded;
		REQUIRE(Palette::Load(path, loaded));
		CHECK(loaded == palette);

		std::filesystem::remove(path);
	};

	TEST_CASE("a palette or a folder of them moved is followed by the sprites painted from it, and no other line or sprite changes")
	{
		const std::filesystem::path folder = Temp("loom sprite relink");
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder / "Units" / "Tanks");
		std::filesystem::create_directories(folder / "Palettes");

		const std::string warm = (folder / "Palettes" / "Warm.loompalette").string();
		const std::string cold = (folder / "Palettes" / "Cold.loompalette").string();
		const std::string hero = (folder / "Hero.loomsprite").string();
		const std::string tank = (folder / "Units" / "Tanks" / "Tank.loomsprite").string();
		const std::string ice = (folder / "Ice.loomsprite").string();
		const SpriteAsset sprite = TwoByTwo();

		REQUIRE(SpriteAsset::Save(hero, sprite, { (folder / "Hero.png").string(), warm }));
		REQUIRE(SpriteAsset::Save(tank, sprite, { (folder / "Units" / "Tanks" / "Tank.png").string(), warm }));
		REQUIRE(SpriteAsset::Save(ice, sprite, { (folder / "Ice.png").string(), cold }));

		const auto links = [](const std::string& path)
		{
			SpriteAsset loaded;
			SpriteAsset::Links loaded_links;
			SpriteAsset::Load(path, loaded, loaded_links);
			return loaded_links;
		};

		const std::string hot = (folder / "Palettes" / "Hot.loompalette").string();

		CHECK(SpriteAsset::RelinkPalettes(folder.string(), warm, hot) == 2);
		CHECK(links(hero).palette == hot);
		CHECK(links(hero).image == (folder / "Hero.png").string());
		CHECK(links(tank).palette == hot);
		CHECK(links(ice).palette == cold);

		const std::filesystem::path moved = folder / "Units" / "Palettes";

		CHECK(SpriteAsset::RelinkPalettes(folder.string(), (folder / "Palettes").string(), moved.string()) == 3);
		CHECK(links(hero).palette == (moved / "Hot.loompalette").string());
		CHECK(links(tank).palette == (moved / "Hot.loompalette").string());
		CHECK(links(ice).palette == (moved / "Cold.loompalette").string());

		CHECK(SpriteAsset::RelinkPalettes(folder.string(), warm, hot) == 0);

		std::filesystem::remove_all(folder);
	};

	#pragma endregion
};

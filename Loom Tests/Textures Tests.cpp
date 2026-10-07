#include "doctest.h"

#include "Test Support.h"

#include "Textures.h"

#include <stdexcept>
#include <vector>


namespace
{
	constexpr int RGBA = 4;
	constexpr uint8_t OPAQUE = 255;
	constexpr uint8_t CLEAR = 0;
	constexpr uint8_t FULL = 255;

	// Two by two, top row red then clear, bottom row green then blue.
	constexpr int SIZE = 2;
	const std::vector<uint8_t> TOP_FIRST =
	{
		FULL, 0, 0, OPAQUE,		0, 0, 0, CLEAR,
		0, FULL, 0, OPAQUE,		0, 0, FULL, OPAQUE,
	};
};


TEST_SUITE("Texture")
{
	// No test constructs an Engine, so there is no renderer: a texture decodes
	// its pixels but has no handle.

	TEST_CASE("a texture decodes its file to RGBA, bottom row first")
	{
		const Loom::Texture texture(LoomTests::WriteTga("loom texture rows.tga", SIZE, SIZE, TOP_FIRST));

		CHECK(texture.width == SIZE);
		CHECK(texture.height == SIZE);
		REQUIRE(texture.pixels.size() == (size_t)SIZE * SIZE * RGBA);

		const std::vector<uint8_t> bottom_first =
		{
			0, FULL, 0, OPAQUE,		0, 0, FULL, OPAQUE,
			FULL, 0, 0, OPAQUE,		0, 0, 0, CLEAR,
		};

		CHECK(texture.pixels == bottom_first);
	};

	TEST_CASE("a texture keeps a transparent pixel transparent")
	{
		const Loom::Texture texture(LoomTests::WriteTga("loom texture alpha.tga", SIZE, SIZE, TOP_FIRST));

		constexpr size_t TOP_RIGHT_ALPHA = (SIZE + 1) * RGBA + 3;

		CHECK(texture.pixels[TOP_RIGHT_ALPHA] == CLEAR);
	};

	TEST_CASE("without a renderer a texture has no handle")
	{
		const Loom::Texture texture(LoomTests::WriteTga("loom texture handle.tga", SIZE, SIZE, TOP_FIRST));

		CHECK(texture.handle == 0);
	};

	TEST_CASE("a missing file throws")
	{
		CHECK_THROWS_AS(Loom::Texture("no such texture.png"), std::runtime_error);
	};

	TEST_CASE("Shared hands out one texture per path")
	{
		const std::string path = LoomTests::WriteTga("loom texture shared.tga", SIZE, SIZE, TOP_FIRST);

		Loom::Texture* first = Loom::Texture::Shared(path);

		REQUIRE(first != nullptr);
		CHECK(Loom::Texture::Shared(path) == first);
	};

	TEST_CASE("Shared answers null for a file that will not load")
	{
		CHECK(Loom::Texture::Shared("no such shared texture.png") == nullptr);
	};
};

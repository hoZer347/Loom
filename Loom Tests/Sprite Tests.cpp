#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "Scene.h"
#include "Sprite.h"
#include "Textures.h"

#include "glm/glm.hpp"

#include <cmath>
#include <vector>

using LoomTests::Pump;

static constexpr float EPSILON = 1e-4f;

namespace
{
	constexpr int WIDTH = 4;
	constexpr int HEIGHT = 2;
	constexpr int RGBA = 4;
	constexpr uint8_t OPAQUE = 255;

	std::string WriteOpaque(const std::string& name)
	{
		return LoomTests::WriteTga(name, WIDTH, HEIGHT, std::vector<uint8_t>(WIDTH * HEIGHT * RGBA, OPAQUE));
	};

	glm::vec3 Corner(const glm::mat4& quad, float x, float y)
	{
		return glm::vec3(quad * glm::vec4(x, y, 0.0f, 1.0f));
	};

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < EPSILON;
	};
};


TEST_SUITE("Sprite")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sprite is a built-in component the editor can add by name")
	{
		Loom::Scene scene("sprite by name");
		Pump();

		Loom::ComponentBase* sprite = Loom::ComponentRegistry::Create("Sprite", scene.GetRoot());
		Pump();

		REQUIRE(sprite != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::Sprite>() == sprite);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sprite casts shadows and cuts out at half alpha by default")
	{
		Loom::Scene scene("sprite defaults");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		CHECK(sprite->castShadows);
		CHECK(sprite->alphaCutoff == doctest::Approx(0.5f));
		CHECK(sprite->GetTexture() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sprite is sized by its pixels and pixelsPerUnit, standing on its pivot")
	{
		Loom::Scene scene("sprite size");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		constexpr float PIXELS_PER_UNIT = 2.0f;

		sprite->texturePath = WriteOpaque("loom sprite size.tga");
		sprite->pixelsPerUnit = PIXELS_PER_UNIT;

		const glm::mat4 quad = sprite->QuadMatrix();

		// Pivot defaults to the bottom middle.
		const float half_width = WIDTH / PIXELS_PER_UNIT / 2.0f;
		const float height = HEIGHT / PIXELS_PER_UNIT;

		CHECK(Near(Corner(quad, 0.0f, 0.0f), glm::vec3(-half_width, 0.0f, 0.0f)));
		CHECK(Near(Corner(quad, 1.0f, 1.0f), glm::vec3(half_width, height, 0.0f)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the pivot is the point of the image that sits on the origin")
	{
		Loom::Scene scene("sprite pivot");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		sprite->texturePath = WriteOpaque("loom sprite pivot.tga");
		sprite->pixelsPerUnit = 1.0f;
		sprite->pivot = Loom::Math::vec2<float>{ { 1.0f, 1.0f } };

		CHECK(Near(Corner(sprite->QuadMatrix(), 1.0f, 1.0f), glm::vec3(0.0f)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a zero pixelsPerUnit leaves the quad finite")
	{
		Loom::Scene scene("sprite zero ppu");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		sprite->texturePath = WriteOpaque("loom sprite zero ppu.tga");
		sprite->pixelsPerUnit = 0.0f;

		const glm::vec3 corner = Corner(sprite->QuadMatrix(), 1.0f, 1.0f);

		CHECK(std::isfinite(corner.x));
		CHECK(std::isfinite(corner.y));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "changing the path loads the new texture")
	{
		Loom::Scene scene("sprite reload");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		const std::string first = WriteOpaque("loom sprite first.tga");
		const std::string second = WriteOpaque("loom sprite second.tga");

		sprite->texturePath = first;
		REQUIRE(sprite->GetTexture() != nullptr);
		CHECK(sprite->GetTexture()->file_path == first);

		sprite->texturePath = second;
		REQUIRE(sprite->GetTexture() != nullptr);
		CHECK(sprite->GetTexture()->file_path == second);

		sprite->texturePath = std::string();
		CHECK(sprite->GetTexture() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a missing texture leaves the sprite with nothing to draw")
	{
		Loom::Scene scene("sprite missing");
		Loom::Sprite* sprite = scene.Attach<Loom::Sprite>();
		Pump();

		sprite->texturePath = std::string("no such sprite.png");

		CHECK(sprite->GetTexture() == nullptr);
	};
};

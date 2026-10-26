#include "doctest.h"

#include "Render Support.h"

#ifndef __EMSCRIPTEN__

#include "Camera.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Image;
using LoomTests::Pump;
using LoomTests::RenderFixture;


namespace
{
	constexpr float CLEAR[] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const glm::u8vec3 CLEARED(0, 0, 255);
	const glm::u8vec3 RED(255, 0, 0);

	// Placed in clip space, whatever the camera, and red.
	const std::string CLIP_SPACE_RED =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"void main() { gl_Position = vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

	// Moved by its model matrix alone, and as red as its seed.
	const std::string SEED_AS_RED =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_model;\n"
		"void main() { gl_Position = u_model * vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"uniform float u_seed;\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(u_seed, 0.0, 0.0, 1.0); }\n";

	// Half of clip space wide, centred on its origin.
	const std::vector<float> CENTRED_HALF
	{
		-0.5f, -1.0f, 0.0f,  0.5f, -1.0f, 0.0f,  0.5f, 1.0f, 0.0f,
		-0.5f, -1.0f, 0.0f,  0.5f,  1.0f, 0.0f, -0.5f, 1.0f, 0.0f,
	};

	// The right half of clip space.
	const std::vector<float> RIGHT_HALF
	{
		0.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
	};

	// Puts Engine::clearColor back once a case that sets it is done.
	struct ClearedTo
	{
		explicit ClearedTo(const float* colour)
		{
			std::copy_n(Loom::Engine::clearColor, std::size(m_previous), m_previous);
			std::copy_n(colour, std::size(m_previous), Loom::Engine::clearColor);
		};

		~ClearedTo() { std::copy_n(m_previous, std::size(m_previous), Loom::Engine::clearColor); };

	private:
		float m_previous[std::size(Loom::Engine::clearColor)];
	};

	uint32_t BigEndian(const std::vector<char>& bytes, size_t at)
	{
		constexpr int BYTE = 8;
		uint32_t value = 0;

		for (size_t i = at; i < at + sizeof(uint32_t); i++)
			value = (value << BYTE) | (uint8_t)bytes[i];

		return value;
	};
};


TEST_SUITE("Render")
{
	TEST_CASE_FIXTURE(RenderFixture, "a frame reads back the size the window was opened, top row first, in the colour it was cleared to")
	{
		const ClearedTo cleared(CLEAR);

		const Image image = Draw([]() { });

		CHECK(image.width == DEFAULT_SIZE.x);
		CHECK(image.height == DEFAULT_SIZE.y);
		CHECK(image.At(0, 0) == CLEARED);
		CHECK(image.At(image.width - 1, image.height - 1) == CLEARED);
	};

	TEST_CASE_FIXTURE(RenderFixture, "a whole frame draws the scenes as a game does")
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(CLIP_SPACE_RED);
		Loom::Shader shader("render right half", source);

		Loom::Scene scene("render right half");
		Pump();

		scene.Attach<Loom::Camera>();
		Loom::GameObject* half = scene.AddChild("Right Half");
		Pump();

		half->Attach<Loom::Material>()->shader = &shader;
		half->Attach<Loom::Mesh>()->m_vertices = RIGHT_HALF;
		Pump();

		const Image image = Frame();
		const int middle = image.height / 2;

		image.Save("Render - right half");

		CHECK(image.At(0, middle) == CLEARED);
		CHECK(image.At(image.width / 4, middle) == CLEARED);
		CHECK(image.At(image.width * 3 / 4, middle) == RED);
		CHECK(image.At(image.width - 1, middle) == RED);
	};

	TEST_CASE_FIXTURE(RenderFixture, "alike meshes drawn as instances each read their own seed, the one they read when drawn alone")
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(SEED_AS_RED);
		Loom::Shader shader("render seeds", source);
		REQUIRE(shader.instanced_id != 0);

		Loom::Scene scene("render seeds");
		Pump();

		scene.Attach<Loom::Camera>();

		constexpr float HALF_OFFSET = 0.5f;
		Loom::GameObject* left = scene.AddChild("Left");
		Loom::GameObject* right = scene.AddChild("Right");
		Pump();

		left->transform.position = glm::vec3(-HALF_OFFSET, 0.0f, 0.0f);
		right->transform.position = glm::vec3(HALF_OFFSET, 0.0f, 0.0f);

		for (Loom::GameObject* half : { left, right })
		{
			half->Attach<Loom::Material>()->shader = &shader;
			half->Attach<Loom::Mesh>()->m_vertices = CENTRED_HALF;
		};

		const Loom::Guid first(1, 2);
		left->SetGuid(first);
		right->SetGuid(Loom::Guid(3, 4));
		Pump();

		const auto reds = [this]()
		{
			const Image image = Frame();
			const int middle = image.height / 2;

			return std::pair(image.At(image.width / 4, middle).r, image.At(image.width * 3 / 4, middle).r);
		};

		const auto [instanced, other] = reds();
		CHECK(instanced != other);

		right->SetGuid(first);
		CHECK(reds().second == instanced);

		right->GetComponent<Loom::Mesh>()->m_draw_type = Loom::Mesh::Dynamic;
		CHECK(reds() == std::pair(instanced, instanced));
	};

	TEST_CASE_FIXTURE(RenderFixture, "a screenshot is a PNG of the frame in the suite's screenshot folder")
	{
		// Where a PNG's width and height are, after its signature and the
		// first chunk's length and type.
		constexpr size_t WIDTH_AT = 16;
		constexpr size_t HEIGHT_AT = 20;
		const std::string SIGNATURE = "\x89PNG\r\n\x1a\n";

		const Image image = Draw([]() { });
		const std::filesystem::path path = image.Save("Render - screenshot");

		CHECK(path.parent_path() == LoomTests::ScreenshotFolder());
		REQUIRE(std::filesystem::exists(path));

		std::ifstream file(path, std::ios::binary);
		const std::vector<char> bytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		REQUIRE(bytes.size() > HEIGHT_AT + sizeof(uint32_t));
		CHECK(std::string(bytes.begin(), bytes.begin() + SIGNATURE.size()) == SIGNATURE);
		CHECK(BigEndian(bytes, WIDTH_AT) == (uint32_t)image.width);
		CHECK(BigEndian(bytes, HEIGHT_AT) == (uint32_t)image.height);
	};
};

#endif

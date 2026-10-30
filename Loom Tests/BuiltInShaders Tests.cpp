#include "doctest.h"

#include "Render Support.h"

#include "BuiltInShaders.h"
#include "ShaderVariables.h"

#include <filesystem>
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN__

#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using LoomTests::Image;
using LoomTests::Pump;
using LoomTests::RenderFixture;

#pragma region Helpers

namespace
{
	constexpr float CLEAR[] = { 0.0f, 0.0f, 1.0f, 1.0f };

	// Half red over the blue it is cleared to, either way a pixel rounds.
	constexpr int HALF = 128;
	constexpr int ROUNDING = 1;

	constexpr float TRANSPARENCY = 0.5f;

	const glm::vec3 RED(1.0f, 0.0f, 0.0f);
	const glm::vec3 CAMERA_POSITION(0.0f, 0.0f, 5.0f);

	// Over the whole view from CAMERA_POSITION, facing it, with the same UV
	// everywhere.
	const std::vector<float> COVERING = { -50.0f, -50.0f, 0.0f, 50.0f, -50.0f, 0.0f, 0.0f, 50.0f, 0.0f };
	const std::vector<float> COVERING_UV = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

	struct VulkanRenderFixture : RenderFixture
	{
		VulkanRenderFixture() : RenderFixture(DEFAULT_SIZE, Loom::Backend::Vulkan) { };
	};

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

	// The middle pixel of a frame of a red triangle drawn with the shader at
	// path, under a Light when lit, set up further by configure.
	template <typename Configure>
	glm::u8vec3 Drawn(RenderFixture& fixture, const std::string& path, bool lit, Configure configure)
	{
		const ClearedTo cleared(CLEAR);

		Loom::Scene scene(path);
		Pump();

		if (lit)
			scene.Attach<Loom::Light>();

		Loom::GameObject* eye = scene.AddChild("Camera");
		Loom::GameObject* shape = scene.AddChild("Shape");
		Pump();

		eye->Attach<Loom::Camera>();
		eye->transform.position = CAMERA_POSITION;

		Loom::Material* material = shape->Attach<Loom::Material>();
		material->SetShaderPath(path);
		material->color = RED;
		configure(*material);

		Loom::Mesh* mesh = shape->Attach<Loom::Mesh>();
		mesh->m_vertices = COVERING;
		mesh->m_textureCoordinates = COVERING_UV;
		Pump();

		REQUIRE(material->shader != nullptr);

		const Image image = fixture.Frame();

		image.Save("BuiltInShaders - " + std::filesystem::path(path).stem().string());

		return image.At(image.width / 2, image.height / 2);
	};

	glm::u8vec3 Drawn(RenderFixture& fixture, const std::string& path, bool lit = false)
	{
		return Drawn(fixture, path, lit, [](Loom::Material&) { });
	};

	// A green texture written beside the screenshots, for as long as a run.
	std::string GreenTexture()
	{
		constexpr int SIDE = 2;

		Image image{ SIDE, SIDE };

		for (int pixel = 0; pixel < SIDE * SIDE; pixel++)
			image.rgb.insert(image.rgb.end(), { 0, 255, 0 });

		return image.Save("BuiltInShaders - green").string();
	};

	// Compiled shaders are kept by path for the process, past the Engine closing,
	// so each backend names them from a folder spelled its own way.
	void CheckDraws(RenderFixture& fixture, const std::string& folder)
	{
		CHECK(Drawn(fixture, folder + "Unlit.shader") == glm::u8vec3(255, 0, 0));

		const glm::u8vec3 lit = Drawn(fixture, folder + "Lit.shader", true);

		CHECK(lit.r > 0);
		CHECK(lit.r < 255);

		const glm::u8vec3 see_through = Drawn(fixture, folder + "Transparent.shader", false,
			[](Loom::Material& material) { material.variables->Set("u_transparency", Loom::UniformType::Float, { TRANSPARENCY }); });

		CHECK(std::abs(see_through.r - HALF) <= ROUNDING);
		CHECK(std::abs(see_through.b - HALF) <= ROUNDING);

		CHECK(Drawn(fixture, folder + "Lit.shader", true,
			[&folder](Loom::Material& material) { material.ChangeShader(folder + "Unlit.shader"); }) == glm::u8vec3(255, 0, 0));

		const std::string green = GreenTexture();
		const auto textured = [&green](Loom::Material& material)
			{
				material.color = glm::vec3(1.0f);
				material.variables->SetTexture("u_texture", green);
			};

		CHECK(Drawn(fixture, folder + "UnlitTextured.shader", false, textured) == glm::u8vec3(0, 255, 0));

		const glm::u8vec3 lit_textured = Drawn(fixture, folder + "LitTextured.shader", true, textured);

		CHECK(lit_textured.g > 0);
		CHECK(lit_textured.r == 0);
	};
};

#pragma endregion

TEST_SUITE("BuiltInShaders")
{
	#pragma region Drawing

	TEST_CASE_FIXTURE(RenderFixture, "every built-in shader draws from the engine with no file on disk, on OpenGL")
	{
		CheckDraws(*this, "Loom/");
	};

	TEST_CASE_FIXTURE(VulkanRenderFixture, "every built-in shader draws from the engine with no file on disk, on Vulkan")
	{
		CheckDraws(*this, "./Loom/");
	};

	#pragma endregion
};

#endif

TEST_SUITE("BuiltInShaders")
{
	#pragma region Queries

	TEST_CASE("each built-in shader's path finds its source, and no other path finds one")
	{
		for (const std::string& path : Loom::BuiltInShaders::Paths())
		{
			INFO(path);
			CHECK(path.starts_with("Loom/"));
			CHECK(Loom::BuiltInShaders::SourceOf(path) != nullptr);
		};

		CHECK(Loom::BuiltInShaders::SourceOf("Assets/Lit.shader") == nullptr);
		CHECK(Loom::BuiltInShaders::SourceOf("") == nullptr);
	};

	TEST_CASE("a path is matched however it is spelled")
	{
		CHECK(Loom::BuiltInShaders::SourceOf("Loom/./Lit.shader") != nullptr);
	};

	TEST_CASE("the built-in shaders say how the engine treats them")
	{
		const Loom::ShaderSettings lit = Loom::ParseShaderSettings(Loom::BuiltInShaders::SourceOf("Loom/Lit.shader"));
		const Loom::ShaderSettings unlit = Loom::ParseShaderSettings(Loom::BuiltInShaders::SourceOf("Loom/Unlit.shader"));
		const Loom::ShaderSettings transparent = Loom::ParseShaderSettings(Loom::BuiltInShaders::SourceOf("Loom/Transparent.shader"));

		CHECK(lit.shading);
		CHECK(lit.shadows);
		CHECK_FALSE(unlit.shading);
		CHECK_FALSE(unlit.shadows);
		CHECK(transparent.transparency);
	};

	TEST_CASE("the built-in shaders declare what a material fills in")
	{
		const auto declares = [](const char* path, const char* name)
			{
				const std::vector<Loom::ShaderVariable> variables = Loom::ParseShaderVariables(Loom::BuiltInShaders::SourceOf(path));

				return std::any_of(variables.begin(), variables.end(), [name](const Loom::ShaderVariable& variable) { return variable.name == name; });
			};

		CHECK(declares("Loom/Transparent.shader", "u_transparency"));
		CHECK(declares("Loom/LitTextured.shader", "u_texture"));
		CHECK(declares("Loom/UnlitTextured.shader", "u_texture"));
	};

	#pragma endregion
};

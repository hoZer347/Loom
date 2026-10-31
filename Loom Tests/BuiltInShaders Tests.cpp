#include "doctest.h"

#include "Render Support.h"
#include "Test Support.h"

#include "BuiltInShaders.h"
#include "Shaders.h"
#include "ShaderVariables.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN__

#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "PixelShader.h"
#include "Scene.h"
#include "UiScript.h"

#include "imgui.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

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

	// A light along this leaves a face toward the camera short of full.
	const glm::vec3 SIDELONG(0.8f, -0.4f, -0.45f);

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
		{
			Loom::GameObject* sun = scene.AddChild("Sun");
			Pump();

			sun->Attach<Loom::Light>();
			sun->transform.rotation = Loom::Light::RotationAlong(SIDELONG);
		};

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

	// A green texture written beside the screenshots. Textures too are kept by
	// path for the process, so each backend has its own.
	std::string GreenTexture(const std::string& backend)
	{
		constexpr int SIDE = 2;

		Image image{ SIDE, SIDE };

		for (int pixel = 0; pixel < SIDE * SIDE; pixel++)
			image.rgb.insert(image.rgb.end(), { 0, 255, 0 });

		return image.Save("BuiltInShaders - green on " + backend).string();
	};

	// Compiled shaders are kept by path for the process, past the Engine closing,
	// so each backend names them from a folder spelled its own way.
	void CheckDraws(RenderFixture& fixture, const std::string& folder, const std::string& backend)
	{
		CHECK(Drawn(fixture, folder + "Unlit.shader") == glm::u8vec3(255, 0, 0));

		const glm::u8vec3 lit = Drawn(fixture, folder + "Lit.shader", true);

		CHECK(lit.r > 0);
		CHECK(lit.r < 255);

		const glm::u8vec3 seeThrough = Drawn(fixture, folder + "Transparent.shader", false,
			[](Loom::Material& material) { material.variables->Set("u_transparency", Loom::UniformType::Float, { TRANSPARENCY }); });

		CHECK(std::abs(seeThrough.r - HALF) <= ROUNDING);
		CHECK(std::abs(seeThrough.b - HALF) <= ROUNDING);

		CHECK(Drawn(fixture, folder + "Lit.shader", true,
			[&folder](Loom::Material& material) { material.ChangeShader(folder + "Unlit.shader"); }) == glm::u8vec3(255, 0, 0));

		const std::string green = GreenTexture(backend);
		const auto textured = [&green](Loom::Material& material)
			{
				material.color = glm::vec3(1.0f);
				material.variables->SetTexture("u_texture", green);
			};

		CHECK(Drawn(fixture, folder + "UnlitTextured.shader", false, textured) == glm::u8vec3(0, 255, 0));

		const glm::u8vec3 litTextured = Drawn(fixture, folder + "LitTextured.shader", true, textured);

		CHECK(litTextured.g > 0);
		CHECK(litTextured.r == 0);
	};

	// The left half of the view in red, over blue, drawn with the shader at path
	// and seen through the bloom pass at bloom. Its edge is the middle column.
	Image Bloomed(RenderFixture& fixture, const std::string& path, const std::string& bloom)
	{
		const std::vector<float> LEFT_HALF = { 0.0f, -50.0f, 0.0f, 0.0f, 50.0f, 0.0f, -100.0f, 0.0f, 0.0f };

		const ClearedTo cleared(CLEAR);

		Loom::Scene scene(bloom);
		Pump();

		Loom::GameObject* eye = scene.AddChild("Camera");
		Loom::GameObject* shape = scene.AddChild("Shape");
		Loom::GameObject* screen = scene.AddChild("Screen");
		Pump();

		eye->Attach<Loom::Camera>();
		eye->transform.position = CAMERA_POSITION;

		Loom::Material* material = shape->Attach<Loom::Material>();
		material->SetShaderPath(path);
		material->color = RED;

		shape->Attach<Loom::Mesh>()->m_vertices = LEFT_HALF;

		Loom::PixelShader* pixel = screen->Attach<Loom::PixelShader>();
		pixel->ChangeShader(bloom);
		Pump();

		REQUIRE(material->shader != nullptr);
		REQUIRE(pixel->shader != nullptr);

		const Image image = fixture.Frame();

		image.Save("BuiltInShaders - bloom of " + std::filesystem::path(path).stem().string());

		return image;
	};

	// Spelled apart from CheckDraws's folders, as compiled shaders are kept by path.
	void CheckBloom(RenderFixture& fixture, const std::string& folder)
	{
		constexpr glm::u8vec3 RED_PIXEL(255, 0, 0);
		constexpr glm::u8vec3 BLUE_PIXEL(0, 0, 255);

		// Its mark's alpha lets a little of what is behind it through.
		constexpr int GLOW_RED = 245;

		// Inside the spread of the edge, and well past it.
		constexpr int NEAR_EDGE = 4;
		constexpr int FAR_FROM_EDGE = 40;

		const int middle = RenderFixture::DEFAULT_SIZE.x / 2;
		const int row = RenderFixture::DEFAULT_SIZE.y / 2;

		const Image glowing = Bloomed(fixture, folder + "Glow.shader", folder + "Bloom.loompixel");

		CHECK(glowing.At(middle - FAR_FROM_EDGE, row).r >= GLOW_RED);
		CHECK(glowing.At(middle + FAR_FROM_EDGE, row) == BLUE_PIXEL);

		const glm::u8vec3 spread = glowing.At(middle + NEAR_EDGE, row);

		CHECK(spread.r > 0);
		CHECK(spread.b < BLUE_PIXEL.b);

		const Image plain = Bloomed(fixture, folder + "Unlit.shader", folder + "Bloom.loompixel");

		CHECK(plain.At(middle - FAR_FROM_EDGE, row) == RED_PIXEL);
		CHECK(plain.At(middle + NEAR_EDGE, row) == BLUE_PIXEL);
	};

	// Runs a UiScript of text over the material's inspector, in a context of its
	// own, and gives back the items it drew.
	std::string InspectorItems(Loom::Material& material, const std::string& text)
	{
		constexpr float WIDTH = 800.0f;
		constexpr float HEIGHT = 600.0f;
		constexpr float FRAME_TIME = 1.0f / 60.0f;
		constexpr int MOST_FRAMES = 600;

		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(WIDTH, HEIGHT);
		io.DeltaTime = FRAME_TIME;
		io.ConfigInputTrickleEventQueue = false;

		unsigned char* pixels = nullptr;
		int atlasWidth = 0;
		int atlasHeight = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);

		std::ostringstream items;

		{
			Loom::UiScript script{ std::filesystem::path() };
			script.LoadText(text);

			for (int frame = 0; frame < MOST_FRAMES && !script.Finished(); frame++)
			{
				script.Step();

				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(WIDTH, HEIGHT));
				ImGui::Begin("Inspector");
				material.OnGui();
				ImGui::End();
				ImGui::EndFrame();
			};

			CHECK(script.Finished());
			CHECK_FALSE(script.Failed());

			script.WriteItems(items);
		};

		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);

		return items.str();
	};

	// A project's folder as the working directory, with its own file at a
	// built-in shader's path, gone once it ends.
	struct ProjectWithLit
	{
		ProjectWithLit() :
			m_previous(std::filesystem::current_path()),
			m_folder(std::filesystem::temp_directory_path() / "loom built-in shader file")
		{
			std::filesystem::remove_all(m_folder);
			std::filesystem::create_directories(m_folder / "Loom");
			std::ofstream(m_folder / "Loom" / "Lit.shader") << "not a shader";
			std::filesystem::current_path(m_folder);
		};

		~ProjectWithLit()
		{
			std::filesystem::current_path(m_previous);

			std::error_code code;
			std::filesystem::remove_all(m_folder, code);
		};

	private:
		std::filesystem::path m_previous;
		std::filesystem::path m_folder;
	};
};

#pragma endregion

TEST_SUITE("BuiltInShaders")
{
	#pragma region Drawing

	TEST_CASE_FIXTURE(RenderFixture, "every built-in shader draws from the engine with no file on disk, on OpenGL")
	{
		CheckDraws(*this, "Loom/", "OpenGL");
	};

	TEST_CASE_FIXTURE(VulkanRenderFixture, "every built-in shader draws from the engine with no file on disk, on Vulkan")
	{
		CheckDraws(*this, "./Loom/", "Vulkan");
	};

	TEST_CASE_FIXTURE(RenderFixture, "the bloom pass spreads Loom/Glow.shader and nothing else, on OpenGL")
	{
		CheckBloom(*this, "Loom/./");
	};

	TEST_CASE_FIXTURE(VulkanRenderFixture, "the bloom pass spreads Loom/Glow.shader and nothing else, on Vulkan")
	{
		CheckBloom(*this, "./Loom/./");
	};

	#pragma endregion

	#pragma region Files

	TEST_CASE_FIXTURE(RenderFixture, "a project's own file at a built-in shader's path is never read or reloaded")
	{
		const ProjectWithLit project;

		Loom::Shader* shader = Loom::Material::SharedShader("Loom/../Loom/Lit.shader");

		REQUIRE(shader != nullptr);

		const uint32_t program = shader->id;

		shader->ReloadIfChanged();

		CHECK(shader->id == program);
	};

	#pragma endregion

	#pragma region Inspector

	TEST_CASE_FIXTURE(RenderFixture, "a built-in shader written into a material's path field is the one it uses")
	{
		Loom::Scene scene("built-in shader field");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		const Loom::SerializedField& path = material->GetFields().front();

		*(std::string*)path.data = "Loom/Unlit.shader";
		material->OnFieldChanged(path);

		REQUIRE(material->shader != nullptr);
		CHECK(material->shader == Loom::Material::SharedShader("Loom/Unlit.shader"));
	};

	TEST_CASE_FIXTURE(RenderFixture, "a built-in pixel pass written into a material's path field leaves its shader as it was")
	{
		Loom::Scene scene("built-in pixel pass in a material");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		material->SetShaderPath("Loom/Unlit.shader");
		Pump();

		Loom::Shader* before = material->shader;
		REQUIRE(before != nullptr);

		const Loom::SerializedField& path = material->GetFields().front();

		*(std::string*)path.data = "Loom/Bloom.loompixel";
		material->OnFieldChanged(path);

		CHECK(material->shader == before);
	};

	TEST_CASE_FIXTURE(RenderFixture, "a built-in pixel pass written into a PixelShader's path field is the one it uses")
	{
		Loom::Scene scene("built-in pixel pass field");
		Pump();

		Loom::PixelShader* pixel = scene.Attach<Loom::PixelShader>();
		Pump();

		const Loom::SerializedField& path = pixel->GetFields().front();

		*(std::string*)path.data = "Loom/Bloom.loompixel";
		pixel->OnFieldChanged(path);

		REQUIRE(pixel->shader != nullptr);
		CHECK(pixel->shader == Loom::Material::SharedShader("Loom/Bloom.loompixel"));
	};

	TEST_CASE_FIXTURE(RenderFixture, "a built-in shader's inspector has no Edit link, and clicking its settings writes nothing")
	{
		Loom::Scene scene("built-in shader inspector");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		material->ChangeShader(Loom::BuiltInShaders::Lit);

		{
			const LoomTests::Captured captured(std::cerr);
			const std::string items = InspectorItems(*material, "click \"Inspector\" \"Shading\"\nwait 2\n");

			CHECK(items.find("Shading") != std::string::npos);
			CHECK(items.find("Edit ") == std::string::npos);
			CHECK_FALSE(captured.Says("Material:"));
		};

		*(std::string*)material->GetFields().front().data = "Assets/Own.shader";

		CHECK(InspectorItems(*material, "wait 2\n").find("Edit Assets/Own.shader") != std::string::npos);
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

	TEST_CASE("each kind of file lists only its own built-ins")
	{
		const std::vector<std::string> passes = Loom::BuiltInShaders::Paths(Loom::Shader::pixel_extension);
		const std::vector<std::string> shaders = Loom::BuiltInShaders::Paths(Loom::Shader::extension);

		CHECK(passes == std::vector<std::string>{ "Loom/Bloom.loompixel" });
		CHECK(shaders.size() == Loom::BuiltInShaders::Paths().size() - passes.size());
		CHECK(std::find(shaders.begin(), shaders.end(), "Loom/Glow.shader") != shaders.end());
		CHECK(std::find(shaders.begin(), shaders.end(), "Loom/Bloom.loompixel") == shaders.end());
		CHECK(Loom::BuiltInShaders::Paths(".png").empty());
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
		const Loom::ShaderSettings glow = Loom::ParseShaderSettings(Loom::BuiltInShaders::SourceOf("Loom/Glow.shader"));

		CHECK(lit.shading);
		CHECK(lit.shadows);
		CHECK_FALSE(unlit.shading);
		CHECK_FALSE(unlit.shadows);
		CHECK(transparent.transparency);
		CHECK_FALSE(glow.shading);
		CHECK(glow.shadows);
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

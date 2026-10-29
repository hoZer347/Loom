#include "doctest.h"

#include "Render Support.h"
#include "Test Support.h"

#include "Camera.h"
#include "ComputeShader.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "RenderingObjects.h"
#include "Scene.h"
#include "ShaderVariables.h"
#include "Shaders.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


#pragma region Helpers

namespace
{
	const std::string COPY =
		"vec4 Compute(ivec2 cell, int layer)\n"
		"{\n"
		"	return LoomRead(cell, layer);\n"
		"}\n";

	// Layer 0 counts up a quarter each run; layer 1 holds a quarter for each run so far.
	const std::string COUNTER =
		"vec4 Compute(ivec2 cell, int layer)\n"
		"{\n"
		"	if (layer == 1)\n"
		"		return vec4(0.0, float(u_frame + 1) * 0.25, 0.0, 1.0);\n"
		"	return LoomRead(cell + ivec2(1, 0), 0) + vec4(0.25, 0.0, 0.0, 0.0);\n"
		"}\n";

	constexpr int SIZE = 256;
};

#pragma endregion


TEST_SUITE("Compute")
{
	#pragma region Files

	TEST_CASE("a compute file is GLSL around a Compute function, which it must define")
	{
		const Loom::Shader::PixelStages stages = Loom::Shader::AssembleCompute(COPY);

		CHECK(stages.fragment.find(COPY) != std::string::npos);
		CHECK(stages.fragment.find("LoomRead") < stages.fragment.find(COPY));
		CHECK(stages.fragment.find("loom_Out = Compute(") > stages.fragment.find(COPY));

		CHECK_THROWS_AS(Loom::Shader::AssembleCompute("vec4 Other(ivec2 cell, int layer) { return vec4(0.0); }\n"), std::runtime_error);
		CHECK_FALSE(Loom::Shader::CheckCompute("void main() { }\n").empty());
	};

	TEST_CASE("a compute file is known by its extension in any case, and is not a pixel shader")
	{
		CHECK(Loom::Shader::IsCompute("Assets/Swarm.loomcompute"));
		CHECK(Loom::Shader::IsCompute("Assets/Swarm.LoomCompute"));
		CHECK_FALSE(Loom::Shader::IsCompute("Assets/Swarm.loompixel"));
		CHECK_FALSE(Loom::Shader::IsPixel("Assets/Swarm.loomcompute"));
	};

	TEST_CASE("a sampler tagged compute is read from a ComputeShader, and the engine's compute names are not variables")
	{
		const std::vector<Loom::ShaderVariable> variables = Loom::ParseShaderVariables(
			"uniform highp sampler2D Swarm; // compute\n"
			"uniform sampler2D u_albedo;\n"
			"uniform int u_frame;\n"
			"uniform highp sampler2D u_layer0;\n");

		REQUIRE(variables.size() == 2);

		CHECK(variables[0].name == "Swarm");
		CHECK(variables[0].scope == Loom::UniformScope::Compute);
		CHECK(variables[1].scope == Loom::UniformScope::Material);
	};

	#pragma endregion

	#pragma region Running

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a ComputeShader writes one buffer while reading the other, swapping each run")
	{
		RecordingRenderer renderer;
		const std::string path = "Recorded.loomcompute";

		REQUIRE(Loom::Material::EditShader(path, COPY).empty());

		Loom::Scene scene("compute recorded");
		Pump();

		Loom::ComputeShader* compute = scene.AddChild("Recorded")->Attach<Loom::ComputeShader>();
		compute->ChangeShader(path);
		Pump();

		REQUIRE(compute->shader != nullptr);

		Loom::ComputeShader::RunAll();

		REQUIRE(renderer.drawnInto.size() == 1);

		const uint32_t first = renderer.drawnInto.back();

		CHECK(first != RecordingRenderer::WINDOW);
		CHECK(renderer.targetSizes.at(first) == glm::ivec2(SIZE));
		CHECK(Loom::ComputeShader::Output("Recorded") == first);
		CHECK(renderer.textures["u_layer0"] != first);

		Loom::ComputeShader::RunAll();

		REQUIRE(renderer.drawnInto.size() == 2);
		CHECK(renderer.drawnInto.back() != first);
		CHECK(renderer.textures["u_layer0"] == first);
		CHECK(Loom::ComputeShader::Output("Recorded") == renderer.drawnInto.back());

		CHECK(Loom::ComputeShader::Output("Recorded1") == 0);
		CHECK(Loom::ComputeShader::Output("Other") == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a ComputeShader under a disabled GameObject does not run and has no output")
	{
		RecordingRenderer renderer;
		const std::string path = "Hidden.loomcompute";

		REQUIRE(Loom::Material::EditShader(path, COPY).empty());

		Loom::Scene scene("compute disabled");
		Pump();

		Loom::GameObject* parent = scene.AddChild("Parent");
		Loom::ComputeShader* compute = parent->AddChild("Hidden")->Attach<Loom::ComputeShader>();
		compute->ChangeShader(path);
		Pump();

		Loom::ComputeShader::RunAll();
		REQUIRE(Loom::ComputeShader::Output("Hidden") != 0);

		parent->SetDisabled(true);
		const size_t draws = renderer.drawnInto.size();

		Loom::ComputeShader::RunAll();

		CHECK(renderer.drawnInto.size() == draws);
		CHECK(Loom::ComputeShader::Output("Hidden") == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the Rendering menu's Compute Shader is a child with a ComputeShader naming no file, which never runs")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("compute menu");
		Pump();

		Loom::GameObject* made = Loom::RenderingObjects::AddComputeShader(scene.GetRoot());
		Pump();

		REQUIRE(made != nullptr);
		CHECK(made->GetName() == Loom::RenderingObjects::ComputeShaderLabel);

		const Loom::ComputeShader* compute = made->GetComponent<Loom::ComputeShader>();
		REQUIRE(compute != nullptr);
		CHECK(compute->GetShaderPath().empty());

		Loom::ComputeShader::RunAll();

		CHECK(renderer.drawnInto.empty());
	};

	#pragma endregion
};

#ifndef __EMSCRIPTEN__

#pragma region Rendered

namespace
{
	using LoomTests::Image;
	using LoomTests::RenderFixture;

	// The whole of clip space, coloured by the counter's two layers.
	const std::string READS_COUNTER =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"void main() { gl_Position = vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"uniform highp sampler2D Counter; // compute\n"
		"uniform highp sampler2D Counter1; // compute\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(texelFetch(Counter, ivec2(3, 5), 0).r, texelFetch(Counter1, ivec2(3, 5), 0).g, 0.0, 1.0); }\n";

	const std::vector<float> WHOLE
	{
		-1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
		-1.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
	};

	constexpr int LAYERS = 2;
	constexpr int RUNS = 2;
	constexpr int COUNTED = 128;	// 0.25 a run, twice, of 255
	constexpr int ROUNDING = 1;

	struct VulkanComputeFixture : RenderFixture
	{
		VulkanComputeFixture() : RenderFixture(DEFAULT_SIZE, Loom::Backend::Vulkan) { };
	};

	// A frame after the counter has run twice, read through its two layers.
	glm::u8vec3 CountedColour(RenderFixture& fixture, const std::string& backend)
	{
		const std::string path = "Compute " + backend + "/Counter.loomcompute";

		REQUIRE(Loom::Material::EditShader(path, COUNTER).empty());

		std::istringstream source(READS_COUNTER);
		Loom::Shader shader("compute reads counter " + backend, source);

		Loom::Scene scene("compute " + backend);
		Pump();

		scene.Attach<Loom::Camera>();

		Loom::ComputeShader* compute = scene.AddChild("Counter")->Attach<Loom::ComputeShader>();
		compute->ChangeShader(path);

		for (const Loom::SerializedField& field : compute->GetFields())
			if (field.name == "Layers")
				*(int*)field.data = LAYERS;

		Loom::GameObject* view = scene.AddChild("View");
		Pump();

		view->Attach<Loom::Material>()->shader = &shader;
		view->Attach<Loom::Mesh>()->m_vertices = WHOLE;
		Pump();

		Image image;

		for (int run = 0; run < RUNS; run++)
			image = fixture.Frame();

		image.Save("Compute - counted on " + backend);

		return image.At(image.width / 2, image.height / 2);
	};
};

#pragma endregion

TEST_SUITE("Compute")
{
	#pragma region Rendered

	TEST_CASE_FIXTURE(RenderFixture, "a ComputeShader's layers count up on the GPU and are read through the samplers named for it, on OpenGL")
	{
		const glm::u8vec3 colour = CountedColour(*this, "OpenGL");

		CHECK(std::abs(colour.r - COUNTED) <= ROUNDING);
		CHECK(std::abs(colour.g - COUNTED) <= ROUNDING);
	};

	TEST_CASE_FIXTURE(VulkanComputeFixture, "a ComputeShader's layers count up on the GPU and are read through the samplers named for it, on Vulkan")
	{
		REQUIRE(Loom::Engine::backend == Loom::Backend::Vulkan);

		const glm::u8vec3 colour = CountedColour(*this, "Vulkan");

		CHECK(std::abs(colour.r - COUNTED) <= ROUNDING);
		CHECK(std::abs(colour.g - COUNTED) <= ROUNDING);
	};

	#pragma endregion
};

#endif

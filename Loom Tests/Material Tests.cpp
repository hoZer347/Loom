#include "doctest.h"

#include "Test Support.h"

#include "Material.h"
#include "Scene.h"
#include "SerializedField.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

using LoomTests::Pump;

#pragma region Helpers

namespace
{
	// Path, colour, shadows, variables, sampling.
	constexpr size_t FIELDS = 5;

	enum { PATH, COLOR, SHADOWS, VARIABLES, SAMPLING };
};

#pragma endregion


TEST_SUITE("Material")
{
	#pragma region Components

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh material has no shader")
	{
		Loom::Material material;
		Pump();

		CHECK(material.shader == nullptr);
	};

	TEST_CASE("a material is a component and registers itself for rendering")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Material, "Rendering">, Loom::Material>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Material>);
	};

	// Mesh issues the draw, so a material renders safely with no GL context.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rendering a material on its own draws nothing")
	{
		Loom::Scene scene("material render");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		scene.Render();

		CHECK(material->shader == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh finds its material on the same GameObject")
	{
		Loom::Scene scene("material lookup");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(scene.GetRoot().GetComponent<Loom::Material>() == material);
	};

	// The engine compiles one shader per path and shares it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material carries the name of the shader it wants")
	{
		Loom::Scene scene("shader path");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(material->GetShaderPath().empty());

		material->SetShaderPath("Assets/Shader.shader");

		CHECK(material->GetShaderPath() == "Assets/Shader.shader");

		// It is a serialized field, so the scene file carries it too.
		REQUIRE(material->GetFields().size() == FIELDS);
		CHECK(material->GetFields().front().Write() == "\"Assets/Shader.shader\"");

		// Picked in the editor from the project's shader files.
		CHECK(material->GetFields().front().type == Loom::FieldType::Asset);
		CHECK(material->GetFields().front().extensions == std::vector<std::string>{ Loom::Shader::extension });
	};

	#pragma endregion

	#pragma region Fields

	// The path comes first: it was the only field before colour was added, and
	// scene files already saved key it by position.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material is white until given a colour, and saves it after the path")
	{
		Loom::Material material;
		Pump();

		const glm::vec3& color = material.color;
		CHECK(color.x == 1.0f);
		CHECK(color.y == 1.0f);
		CHECK(color.z == 1.0f);

		material.color = glm::vec3(0.25f, 0.5f, 0.75f);

		REQUIRE(material.GetFields().size() == FIELDS);
		CHECK(material.GetFields()[COLOR].Write() == "(0.25, 0.5, 0.75)");
	};

	// After colour, for the same reason colour is after the path.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material is shadowed until told otherwise, and saves its variables last")
	{
		Loom::Material material;
		Pump();

		CHECK(material.shadows);
		CHECK(material.GetFields()[SHADOWS].Write() == "true");

		material.variables->Set("u_speed", Loom::UniformType::Float, { 2.0 });
		material.variables->Set("u_tint", Loom::UniformType::Vec3, { 1.0, 0.5, 0.25 });

		const Loom::SerializedField& field = material.GetFields()[VARIABLES];

		CHECK(field.type == Loom::FieldType::Uniforms);
		CHECK(field.Write() == "\"u_speed float 2\\nu_tint vec3 1 0.5 0.25\\n\"");

		Loom::Material read;
		Pump();

		REQUIRE(read.GetFields()[VARIABLES].Read(field.Write()));
		CHECK(read.variables->Write() == material.variables->Write());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material samples as its shader says until told otherwise, and saves that last")
	{
		Loom::Material material;
		Pump();

		CHECK(material.sampling == Loom::Material::AsShader);

		material.sampling = Loom::Material::Nearest;
		const std::string written = material.GetFields()[SAMPLING].Write();

		Loom::Material read;
		Pump();

		REQUIRE(read.GetFields()[SAMPLING].Read(written));
		CHECK(read.sampling == Loom::Material::Nearest);
	};

	#pragma endregion

	#pragma region Compiling

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with no shader named compiles nothing")
	{
		Loom::Scene scene("no shader named");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(material->shader == nullptr);
	};

#ifndef __EMSCRIPTEN__
	// Native only: a missing path fails in the ifstream here, but needs a browser on the web.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that failed to compile is tried again")
	{
		const LoomTests::Captured captured(std::cerr);

		{
			Loom::Scene scene("first attempt");
			Pump();

			scene.Attach<Loom::Material>()->SetShaderPath("no such shader.shader");
			Pump();
		};

		{
			Loom::Scene scene("second attempt");
			Pump();

			scene.Attach<Loom::Material>()->SetShaderPath("no such shader.shader");
			Pump();
		};

		// Two materials, two attempts: the second one did not get a remembered
		// failure handed back to it.
		CHECK(captured.Count("Material: no such shader.shader:") == 2);
	};

	// Fixing the file then needs no reload: the editor's poll compiles it
	// again once it has been written, and not before.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that failed to compile is compiled again by the poll once its file changes")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom polled shader.shader";
		std::filesystem::remove(path);

		const std::string failure = "Material: " + path.string() + ":";

		const LoomTests::Captured captured(std::cerr);
		const auto attempts = [&]() { return captured.Count(failure); };

		Loom::Scene scene("polled");
		Pump();

		scene.Attach<Loom::Material>()->SetShaderPath(path.string());
		Pump();
		const size_t attached = attempts();

		Loom::Material::ReloadChangedShaders();
		const size_t unchanged = attempts();

		// Written, and still not a shader that compiles here.
		std::ofstream(path) << "// ===FRAGMENT===\n";

		Loom::Material::ReloadChangedShaders();
		const size_t changed = attempts();

		std::filesystem::remove(path);

		CHECK(attached == 1);
		CHECK(unchanged == 1);
		CHECK(changed == 2);
	};
#endif

	#pragma endregion
};

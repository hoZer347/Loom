#include "doctest.h"

#include "Test Support.h"

#include "Material.h"
#include "Scene.h"
#include "SerializedField.h"

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

using LoomTests::Pump;


TEST_SUITE("Material")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh material has no shader")
	{
		Loom::Material material;
		Pump();

		CHECK(material.shader == nullptr);
	};

	TEST_CASE("a material is a component and registers itself for rendering")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Material>, Loom::Material>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Material>);
	};

	// Material overrides OnRender but does nothing in it -- Mesh is what issues
	// the draw call, reading the shader back off the material. Until that moves,
	// rendering a material is safe with no GL context.
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

	// A material names its shader rather than owning one: the engine compiles
	// one per path and shares it, because ~Shader evicts an entry every user of
	// that path is reading.
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
		REQUIRE(material->GetFields().size() == 1);
		CHECK(material->GetFields().front().Write() == "\"Assets/Shader.shader\"");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with no shader named compiles nothing")
	{
		Loom::Scene scene("no shader named");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(material->shader == nullptr);
	};

#ifndef __EMSCRIPTEN__
	// A shader that would not compile is not remembered as broken: fixing the
	// file and reloading the scene has to be enough, or live iteration means
	// restarting the editor.
	//
	// Native only, for the reason the Shader suite gives: a missing path fails
	// in the ifstream here, while the web build fetches over HTTP and needs a
	// browser to fail in.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that failed to compile is tried again")
	{
		std::ostringstream captured;
		std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());

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

		std::cerr.rdbuf(previous);

		// Two materials, two attempts: the second one did not get a remembered
		// failure handed back to it.
		size_t attempts = 0;

		for (size_t at = captured.str().find("no such shader.shader");
			at != std::string::npos;
			at = captured.str().find("no such shader.shader", at + 1))
			attempts++;

		CHECK(attempts == 2);
	};
#endif
};

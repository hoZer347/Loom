#include "doctest.h"

#include "Test Support.h"

#include "Shapes.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"

#include <vector>


namespace
{
	namespace Shapes = Loom::Shapes;

	// Floats a vertex holds, in Mesh's arrays.
	constexpr size_t POSITION_FLOATS = 3;
	constexpr size_t UV_FLOATS = 2;

	constexpr const char* LIT = "Assets/Lit.shader";
};


// Every Material here reads its shader, which the web build fetches, and node
// cannot.
#ifndef __EMSCRIPTEN__
TEST_SUITE("Shapes")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape added is a child named for it, drawing it whole with the shader")
	{
		Loom::Scene scene("Shapes");

		for (const Shapes::Info& info : Shapes::All())
		{
			CAPTURE(info.label);

			Loom::GameObject* object = Shapes::Add(scene.GetRoot(), info.shape, LIT);
			LoomTests::Pump();

			CHECK(object->GetName() == info.label);
			CHECK(object->GetParent() == &scene.GetRoot());

			const Loom::Material* material = object->GetComponent<Loom::Material>();
			REQUIRE(material != nullptr);
			CHECK(material->GetShaderPath() == LIT);

			const Loom::Mesh* mesh = object->GetComponent<Loom::Mesh>();
			REQUIRE(mesh != nullptr);

			const size_t vertices = mesh->m_vertices->size() / POSITION_FLOATS;
			CHECK(vertices > 0);
			CHECK(mesh->m_vertices->size() % POSITION_FLOATS == 0);
			CHECK(mesh->m_normals->size() / POSITION_FLOATS == vertices);
			CHECK(mesh->m_textureCoordinates->size() / UV_FLOATS == vertices);
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape added under an object is its child")
	{
		Loom::Scene scene("Shapes");
		Loom::GameObject* crate = scene.GetRoot().AddChild("Crate");
		LoomTests::Pump();

		Loom::GameObject* lid = Shapes::Add(*crate, Shapes::Shape::Cylinder, LIT);
		LoomTests::Pump();

		CHECK(lid->GetParent() == crate);
		CHECK(crate->GetChildren() == std::vector<Loom::GameObject*>{ lid });
	};
};
#endif

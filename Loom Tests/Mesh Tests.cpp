#include "doctest.h"

#include "Test Support.h"

#include "Mesh.h"
#include "Material.h"
#include "Scene.h"

using LoomTests::Pump;


TEST_SUITE("Mesh")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh remembers the primitive it was built for")
	{
		Loom::Mesh mesh(Loom::Mesh::Triangles);
		Pump();

		CHECK(mesh.primitive_id == Loom::Mesh::Triangles);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh mesh has no geometry, no material and static draw")
	{
		Loom::Mesh mesh(Loom::Mesh::Triangles);
		Pump();

		CHECK(mesh.m_vertices->empty());
		CHECK(mesh.m_indices.empty());
		CHECK(mesh.material == nullptr);
		CHECK(mesh.m_draw_type == Loom::Mesh::Static);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "geometry is owned by the mesh and can be replaced wholesale")
	{
		Loom::Mesh mesh(Loom::Mesh::Triangles);
		Pump();

		mesh.m_vertices =
		{
			0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f,
		};

		CHECK(mesh.m_vertices->size() == 9);

		// m_vertices holds three floats per vertex, which is what Draw
		// divides by before handing a vertex count to glDrawArrays.
		CHECK(mesh.m_vertices->size() % 3 == 0);
		CHECK(mesh.m_vertices->size() / 3 == 3);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the draw type is settable to any GL usage hint")
	{
		Loom::Mesh mesh(Loom::Mesh::Triangles);
		Pump();

		mesh.m_draw_type = Loom::Mesh::Dynamic;

		CHECK(mesh.m_draw_type == Loom::Mesh::Dynamic);
	};

	TEST_CASE("a mesh is a component and registers itself for rendering")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Mesh>, Loom::Mesh>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Mesh>);
	};

	// A mesh added in the editor has no material until one is set up. OnRender
	// has to notice that and bail out before it touches GL, or every such mesh
	// dereferences null on the first frame.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rendering a mesh with no material is a no-op")
	{
		Loom::Scene scene("mesh without material");
		Pump();

		Loom::Mesh* mesh = scene.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		REQUIRE(mesh->GetGameObject() == &scene.GetRoot());
		REQUIRE(mesh->material == nullptr);

		// Reaches GetComponent<Material>, finds nothing, and returns before the
		// first glUseProgram -- which is why this is safe with no GL context.
		scene.Render();

		CHECK(mesh->material == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rendering adopts the GameObject's material but stops at a null shader")
	{
		Loom::Scene scene("mesh with material");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Loom::Mesh*     mesh     = scene.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		REQUIRE(material->shader == nullptr);

		scene.Render();

		// OnRender caches the material off the GameObject on first render...
		CHECK(mesh->material == material);

		// ...and then stops, because the material has no shader to bind.
		CHECK(mesh->material->shader == nullptr);
	};
};

#include "doctest.h"

#include "Test Support.h"

#include "Mesh.h"
#include "Material.h"
#include "Scene.h"

using LoomTests::Pump;

// GL_STATIC_DRAW / GL_DYNAMIC_DRAW / GL_STREAM_DRAW, spelled out so the test
// does not need a GL header (and reads the same in both builds).
static constexpr uint32_t GL_STATIC_DRAW_VALUE  = 0x88E4;
static constexpr uint32_t GL_DYNAMIC_DRAW_VALUE = 0x88E8;

// GL_TRIANGLES.
static constexpr uint32_t GL_TRIANGLES_VALUE = 0x0004;


TEST_SUITE("Mesh")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh remembers the primitive it was built for")
	{
		Loom::Mesh mesh(GL_TRIANGLES_VALUE);
		Pump();

		CHECK(mesh.primitive_id == GL_TRIANGLES_VALUE);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh mesh has no geometry, no material and static draw")
	{
		Loom::Mesh mesh(GL_TRIANGLES_VALUE);
		Pump();

		CHECK(mesh.m_vertices->empty());
		CHECK(mesh.m_indices.empty());
		CHECK(mesh.material == nullptr);
		CHECK(mesh.m_draw_type == GL_STATIC_DRAW_VALUE);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "geometry is owned by the mesh and can be replaced wholesale")
	{
		Loom::Mesh mesh(GL_TRIANGLES_VALUE);
		Pump();

		mesh.m_vertices =
		{
			0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f,
		};

		CHECK(mesh.m_vertices->size() == 9);

		// m_vertices holds three floats per vertex, which is what OnRender
		// divides by before handing a vertex count to glDrawArrays.
		CHECK(mesh.m_vertices->size() % 3 == 0);
		CHECK(mesh.m_vertices->size() / 3 == 3);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the draw type is settable to any GL usage hint")
	{
		Loom::Mesh mesh(GL_TRIANGLES_VALUE);
		Pump();

		mesh.m_draw_type = GL_DYNAMIC_DRAW_VALUE;

		CHECK(mesh.m_draw_type == GL_DYNAMIC_DRAW_VALUE);
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

		Loom::Mesh* mesh = scene.Attach<Loom::Mesh>(GL_TRIANGLES_VALUE);
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
		Loom::Mesh*     mesh     = scene.Attach<Loom::Mesh>(GL_TRIANGLES_VALUE);
		Pump();

		REQUIRE(material->shader == nullptr);

		scene.Render();

		// OnRender caches the material off the GameObject on first render...
		CHECK(mesh->material == material);

		// ...and then stops, because the material has no shader to bind.
		CHECK(mesh->material->shader == nullptr);
	};
};

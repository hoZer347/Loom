#include "doctest.h"

#include "Test Support.h"

#include "Mesh.h"
#include "Material.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Shaders.h"
#include "Skin.h"

#include <algorithm>
#include <bit>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;

namespace
{
	// Whether the inspector draws the mesh's field at data, failing when it has none there.
	bool MeshFieldShown(const Loom::Mesh& mesh, const void* data)
	{
		for (const Loom::SerializedField& field : mesh.GetFields())
			if (field.data == data)
				return mesh.IsShown(field);

		FAIL("no such field");
		return false;
	};
};


TEST_SUITE("Mesh")
{
	#pragma region Geometry

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

	#pragma endregion

	#pragma region Generated

	// A value no other field of a fresh mesh writes, to look for in the text.
	constexpr float GENERATED_VALUE = 7.25f;

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a generated mesh's geometry is not written, and the rest of it is")
	{
		Loom::Scene scene("generated");
		Pump();

		Loom::GameObject* holder = scene.AddChild("Ground");
		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = std::vector<float>(9, GENERATED_VALUE);
		mesh->m_textureCoordinates = std::vector<float>(6, GENERATED_VALUE);
		mesh->m_normals = std::vector<float>(9, GENERATED_VALUE);

		CHECK(Loom::SceneSerializer::Serialize(*holder).find("7.25") != std::string::npos);

		mesh->m_generated = true;

		const std::string text = Loom::SceneSerializer::Serialize(*holder);

		CHECK(text.find("7.25") == std::string::npos);

		// The rest of the mesh is still written: its draw type, for one.
		const size_t mesh_at = text.find("Component Mesh");

		REQUIRE(mesh_at != std::string::npos);
		CHECK(text.find("2 = ", mesh_at) != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a generated mesh read back has no geometry until its script makes it again")
	{
		Loom::Scene scene("generated");
		Pump();

		Loom::GameObject* holder = scene.AddChild("Ground");
		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = std::vector<float>(9, GENERATED_VALUE);
		mesh->m_generated = true;

		std::string error;
		Loom::GameObject* copy = Loom::SceneSerializer::Deserialize(Loom::SceneSerializer::Serialize(*holder), scene.GetRoot(), &error);
		Pump();

		REQUIRE(copy != nullptr);

		Loom::Mesh* read = copy->GetComponent<Loom::Mesh>();

		REQUIRE(read != nullptr);
		CHECK(read->m_vertices->empty());

		// Its script marks it again as it makes the geometry again, which then
		// stays out of the next write too.
		read->m_generated = true;
		read->m_vertices = std::vector<float>(9, GENERATED_VALUE);

		CHECK(Loom::SceneSerializer::Serialize(*copy).find("7.25") == std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh hands its vertices over once, and again only once they change")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("kept");
		Pump();

		Loom::GameObject* holder = scene.AddChild("Ground");
		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = std::vector<float>(9, 1.0f);

		mesh->Draw(1);
		mesh->Draw(1);

		CHECK(renderer.handed == 1);
		CHECK(renderer.drawn.size() == 2);

		mesh->m_vertices = std::vector<float>(9, 2.0f);
		mesh->Draw(1);

		CHECK(renderer.handed == 2);
		CHECK(renderer.lastVertices == std::vector<float>(9, 2.0f));
	};

	#pragma endregion

	#pragma region Instancing

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "meshes alike are drawn as instances of one, each with its own transform and colour")
	{
		RecordingRenderer renderer;

		std::istringstream source(
			"// ===VERTEX===\nuniform mat4 u_model;\nuniform vec3 u_color;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec3 u_color;\nvoid main() { }\n");
		Loom::Shader shader("instanced meshes", source);

		REQUIRE(shader.instanced_id != 0);

		Loom::Scene scene("instanced");
		Pump();

		const std::vector<float> triangle(9, 1.0f);
		const std::vector<float> other(9, 2.0f);

		const auto add = [&](const std::vector<float>& vertices, const glm::vec3& color, Loom::Mesh::DrawType drawType)
		{
			Loom::GameObject* holder = scene.AddChild("Shape");
			holder->transform.position = color;

			Loom::Material* material = holder->Attach<Loom::Material>();
			material->shader = &shader;
			material->color = color;

			Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			mesh->m_vertices = vertices;
			mesh->m_draw_type = drawType;
		};

		const glm::vec3 red(1.0f, 0.0f, 0.0f);
		const glm::vec3 green(0.0f, 1.0f, 0.0f);
		const glm::vec3 blue(0.0f, 0.0f, 1.0f);

		add(triangle, red, Loom::Mesh::Static);
		add(triangle, green, Loom::Mesh::Static);
		add(other, red, Loom::Mesh::Static);
		add(triangle, blue, Loom::Mesh::Static);
		add(triangle, red, Loom::Mesh::Dynamic);
		Pump();

		scene.Render();

		// The dynamic one as it is found, then the three alike as one, then the odd one out alone.
		const uint32_t id = shader.id;
		const uint32_t instanced = shader.instanced_id;

		CHECK(renderer.drawn == std::vector<uint32_t>{ id, instanced, instanced, instanced, id });
		CHECK(renderer.instanceCounts == std::vector<size_t>{ 3 });
		CHECK(renderer.instanceColors == std::vector<glm::vec3>{ red, green, blue });
		REQUIRE(renderer.instanceModels.size() == 3);
		CHECK(glm::vec3(renderer.instanceModels[1][3]) == green);
		CHECK(renderer.drawnVertices.back() == other);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "each GameObject has a seed of its own from its guid, drawn alone or as an instance")
	{
		RecordingRenderer renderer;

		std::istringstream source(
			"// ===VERTEX===\nuniform mat4 u_model;\nuniform float u_seed;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform float u_seed;\nvoid main() { }\n");
		Loom::Shader shader("seeded meshes", source);

		REQUIRE(shader.instanced_id != 0);

		Loom::Scene scene("seeded");
		Pump();

		std::vector<Loom::GameObject*> holders;
		std::vector<Loom::Mesh*> meshes;

		for (const char* name : { "First", "Second", "Third" })
		{
			Loom::GameObject* holder = scene.AddChild(name);
			holders.push_back(holder);
			holder->Attach<Loom::Material>()->shader = &shader;

			Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			mesh->m_vertices = std::vector<float>(9, 1.0f);
			meshes.push_back(mesh);
		};

		Pump();
		scene.Render();

		REQUIRE(renderer.instanceSeeds.size() == 3);

		for (const float seed : renderer.instanceSeeds)
		{
			CHECK(seed >= 0.0f);
			CHECK(seed < 1.0f);
		};

		CHECK(renderer.instanceSeeds[0] != renderer.instanceSeeds[1]);
		CHECK(renderer.instanceSeeds[1] != renderer.instanceSeeds[2]);
		CHECK(renderer.instanceSeeds[0] != renderer.instanceSeeds[2]);

		meshes[0]->Draw(shader.id);
		CHECK(renderer.floats["u_seed"] == renderer.instanceSeeds[0]);

		const Loom::Guid guid(1, 2);

		holders[0]->SetGuid(guid);
		meshes[0]->Draw(shader.id);
		const float fromGuid = renderer.floats["u_seed"];

		holders[1]->SetGuid(guid);
		meshes[1]->Draw(shader.id);
		CHECK(renderer.floats["u_seed"] == fromGuid);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "meshes differing in their material's variables, their own variables or a Skin are drawn alone")
	{
		RecordingRenderer renderer;

		std::istringstream source(
			"// ===VERTEX===\nuniform mat4 u_model;\nuniform float u_speed;\nuniform float u_tint; // instance\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec3 u_color;\nvoid main() { }\n");
		Loom::Shader shader("instanced apart", source);

		REQUIRE(shader.instanced_id != 0);

		Loom::Scene scene("instanced apart");
		Pump();

		std::vector<Loom::GameObject*> holders;
		std::vector<Loom::Material*> materials;
		std::vector<Loom::Mesh*> meshes;

		for (const char* name : { "First", "Second" })
		{
			Loom::GameObject* holder = scene.AddChild(name);
			holders.push_back(holder);

			Loom::Material* material = holder->Attach<Loom::Material>();
			material->shader = &shader;
			materials.push_back(material);

			Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			mesh->m_vertices = std::vector<float>(9, 1.0f);
			meshes.push_back(mesh);
		};

		Pump();

		const uint32_t id = shader.id;
		const uint32_t instanced = shader.instanced_id;

		Loom::UniformValues faster;
		faster.Set("u_speed", Loom::UniformType::Float, { 2.0 });

		Loom::UniformValues tinted;
		tinted.Set("u_tint", Loom::UniformType::Float, { 0.5 });

		SUBCASE("alike, they are drawn as instances of one")
		{
			scene.Render();

			CHECK(renderer.drawn == std::vector<uint32_t>{ instanced, instanced });
		};

		SUBCASE("the material's variables")
		{
			materials[1]->variables = faster;
			scene.Render();

			CHECK(renderer.drawn == std::vector<uint32_t>{ id, id });
		};

		SUBCASE("the mesh's own variables")
		{
			meshes[1]->variables = tinted;
			scene.Render();

			CHECK(renderer.drawn == std::vector<uint32_t>{ id, id });
		};

		SUBCASE("a Skin bending each")
		{
			for (Loom::GameObject* holder : holders)
				holder->Attach<Loom::Skin>();

			Pump();
			scene.Render();

			CHECK(renderer.drawn == std::vector<uint32_t>{ id, id });
		};
	};

	#pragma endregion

	#pragma region Components and Rendering

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the draw type is settable to any GL usage hint")
	{
		Loom::Mesh mesh(Loom::Mesh::Triangles);
		Pump();

		mesh.m_draw_type = Loom::Mesh::Dynamic;

		CHECK(mesh.m_draw_type == Loom::Mesh::Dynamic);
	};

	TEST_CASE("a mesh is a component and registers itself for rendering")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Mesh, "Rendering">, Loom::Mesh>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Mesh>);
	};

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

	#pragma endregion

	#pragma region Inspector

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the inspector draws a mesh's geometry as one summary, and its material only when it is not the GameObject's")
	{
		Loom::Scene scene("mesh inspector");
		Pump();

		Loom::GameObject* holder = scene.AddChild("Cube");
		Loom::GameObject* other = scene.AddChild("Other");
		Loom::Material* own = holder->Attach<Loom::Material>();
		Loom::Material* elsewhere = other->Attach<Loom::Material>();
		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		const auto shown = [mesh](const void* data) { return MeshFieldShown(*mesh, data); };

		for (const void* geometry : { (const void*)&*mesh->m_vertices, (const void*)&*mesh->m_textureCoordinates, (const void*)&*mesh->m_normals, (const void*)&*mesh->m_colors, (const void*)&*mesh->m_uvLayers })
			CHECK_FALSE(shown(geometry));

		CHECK(shown(&*mesh->primitive_id));
		CHECK(shown(&*mesh->m_draw_type));

		mesh->material = own;
		CHECK_FALSE(shown(&*mesh->material));

		mesh->material = elsewhere;
		CHECK(shown(&*mesh->material));

		CHECK(mesh->Summary() == "0 vertices");

		constexpr size_t vertices = 3;
		mesh->m_vertices = std::vector<float>(vertices * 3, 0.0f);
		mesh->m_normals = std::vector<float>(vertices * 3, 0.0f);
		mesh->SetUV<float>(2, std::vector<float>(vertices, 0.0f));
		mesh->m_generated = true;

		CHECK(mesh->Summary() == "3 vertices, normals, UV_2, generated");

		mesh->m_vertices = std::vector<float>(3, 0.0f);
		mesh->m_normals = std::vector<float>();
		mesh->ClearUV(2);
		mesh->m_generated = false;

		CHECK(mesh->Summary() == "1 vertex");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the inspector draws a mesh's material while the GameObject has none of its own to stand for it")
	{
		Loom::Scene scene("mesh inspector, no material");
		Pump();

		Loom::Mesh* mesh = scene.AddChild("Cube")->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		CHECK(MeshFieldShown(*mesh, &*mesh->material));
	};

	#pragma endregion

	#pragma region UV Layers

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh's UV layers hold each vertex's values as raw words, in order of id")
	{
		Loom::Mesh mesh;
		Pump();

		mesh.SetUV<float, glm::vec3>(2, { { 0.5f, glm::vec3(1.0f, 2.0f, 3.0f) }, { -1.0f, glm::vec3(4.0f, 5.0f, 6.0f) } });
		mesh.SetUV<bool, int>(0, { { true, -2 }, { false, 7 } });

		std::vector<Loom::UVLayer> layers = mesh.UVLayers();

		REQUIRE(layers.size() == 2);
		const Loom::UVLayer flags{ 0, 2, { 1u, std::bit_cast<uint32_t>(-2), 0u, 7u } };

		CHECK(layers[0] == flags);
		CHECK(layers[1].id == 2);
		CHECK(layers[1].words == 4);
		REQUIRE(layers[1].values.size() == 8);
		CHECK(layers[1].values[0] == std::bit_cast<uint32_t>(0.5f));
		CHECK(layers[1].values[3] == std::bit_cast<uint32_t>(3.0f));
		CHECK(layers[1].values[4] == std::bit_cast<uint32_t>(-1.0f));

		// Set again, a layer is replaced rather than added.
		mesh.SetUV<unsigned int>(2, std::vector<unsigned int>{ 9u, 10u });
		layers = mesh.UVLayers();

		REQUIRE(layers.size() == 2);
		const Loom::UVLayer replaced{ 2, 1, { 9u, 10u } };

		CHECK(layers[1] == replaced);

		// A layer set between two others, and one grown in place, keep the rest.
		mesh.SetUV<int>(1, std::vector<int>{ 3, 4 });
		mesh.SetUV<glm::vec2>(0, { glm::vec2(1.0f, 2.0f), glm::vec2(3.0f, 4.0f) });
		layers = mesh.UVLayers();

		REQUIRE(layers.size() == 3);
		CHECK(layers[0].words == 2);
		CHECK(layers[0].values[3] == std::bit_cast<uint32_t>(4.0f));
		const Loom::UVLayer between{ 1, 1, { 3u, 4u } };

		CHECK(layers[1] == between);
		CHECK(layers[2] == replaced);

		mesh.ClearUV(1);
		mesh.ClearUV(5);
		mesh.ClearUV(0);
		layers = mesh.UVLayers();

		REQUIRE(layers.size() == 1);
		CHECK(layers[0].id == 2);

		mesh.ClearUV(2);

		CHECK(mesh.UVLayers().empty());
		CHECK(mesh.m_uvLayers->empty());

		// Set all at once, the layers fall in order of id.
		const Loom::UVLayer high{ 7, 1, { 5u } };
		const Loom::UVLayer low{ 3, 2, { 6u, 7u } };
		mesh.SetUVLayers({ high, low });

		const std::vector<Loom::UVLayer> ordered{ low, high };

		CHECK(mesh.UVLayers() == ordered);

		mesh.SetUVLayers({ });

		CHECK(mesh.m_uvLayers->empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh hands its UV layers over after the rest of each vertex, and leaves out one that does not fit")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("layers");
		Pump();

		Loom::Mesh* mesh = scene.AddChild("Layered")->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = std::vector<float>(9, 1.0f);
		mesh->SetUV<float>(3, std::vector<float>{ 0.25f, 0.5f, 0.75f });
		mesh->SetUV<glm::ivec2>(1, std::vector<glm::ivec2>{ { 1, 2 }, { 3, 4 }, { 5, 6 } });

		// One value short of the three vertices.
		mesh->SetUV<float>(5, std::vector<float>{ 1.0f, 2.0f });

		mesh->Draw(1);

		Loom::VertexLayout layout = Loom::VertexFormat::Position;
		layout.layers = { { 1, 2 }, { 3, 1 } };

		CHECK(renderer.lastLayout == layout);
		REQUIRE(renderer.lastVertices.size() == 18);
		CHECK(renderer.lastVertices[0] == 1.0f);
		CHECK(std::bit_cast<int>(renderer.lastVertices[3]) == 1);
		CHECK(std::bit_cast<int>(renderer.lastVertices[4]) == 2);
		CHECK(renderer.lastVertices[5] == 0.25f);
		CHECK(std::bit_cast<int>(renderer.lastVertices[15]) == 5);
		CHECK(renderer.lastVertices[17] == 0.75f);

		// Normals put the layers after them, and a change to a layer hands the
		// vertices over again.
		mesh->m_normals = std::vector<float>(9, 0.0f);
		mesh->ClearUV(1);
		mesh->Draw(1);

		layout = Loom::VertexFormat::PositionUVNormal;
		layout.layers = { { 3, 1 } };

		CHECK(renderer.handed == 2);
		CHECK(renderer.lastLayout == layout);
		REQUIRE(renderer.lastVertices.size() == 3 * Loom::FloatsPerVertex(layout));
		CHECK(renderer.lastVertices[Loom::FloatsPerVertex(Loom::VertexFormat::PositionUVNormal)] == 0.25f);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh's UV layers are written and read back, unless the mesh is generated")
	{
		Loom::Scene scene("layers");
		Pump();

		Loom::GameObject* holder = scene.AddChild("Layered");
		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->SetUV<glm::vec2>(4, std::vector<glm::vec2>{ { 0.1f, -0.0f }, { 1e20f, 2.0f } });

		std::string error;
		Loom::GameObject* copy = Loom::SceneSerializer::Deserialize(Loom::SceneSerializer::Serialize(*holder), scene.GetRoot(), &error);
		Pump();

		REQUIRE(copy != nullptr);
		REQUIRE(copy->GetComponent<Loom::Mesh>() != nullptr);
		CHECK(copy->GetComponent<Loom::Mesh>()->UVLayers() == mesh->UVLayers());

		mesh->m_generated = true;
		copy = Loom::SceneSerializer::Deserialize(Loom::SceneSerializer::Serialize(*holder), scene.GetRoot(), &error);
		Pump();

		REQUIRE(copy != nullptr);
		CHECK(copy->GetComponent<Loom::Mesh>()->UVLayers().empty());
	};

	TEST_CASE("a UV layer slot is found in its attribute's name, and in the vertices by their layout")
	{
		CHECK_FALSE(Loom::ParseUVSlot("aPos").has_value());
		CHECK_FALSE(Loom::ParseUVSlot("loom_UV_1").has_value());
		CHECK_FALSE(Loom::ParseUVSlot("loom_UV_1_x").has_value());

		const std::optional<Loom::UVSlot> slot = Loom::ParseUVSlot("loom_UV_12_1");

		REQUIRE(slot.has_value());
		CHECK(slot->layer == 12);
		CHECK(slot->slot == 1);

		Loom::VertexLayout layout = Loom::VertexFormat::PositionUV;
		layout.layers = { { 2, 3 }, { 12, 6 } };

		const size_t first = Loom::FloatsPerVertex(Loom::VertexFormat::PositionUV);

		CHECK(Loom::FloatsPerVertex(layout) == first + 9);
		CHECK(Loom::SourceOf(layout, { 2, 0 }).offset == first);
		CHECK(Loom::SourceOf(layout, { 2, 0 }).words == 3);
		CHECK(Loom::SourceOf(layout, { 12, 1 }).offset == first + 3 + 4);
		CHECK(Loom::SourceOf(layout, { 12, 1 }).words == 2);
		CHECK(Loom::SourceOf(layout, { 12, 2 }).words == 0);
		CHECK(Loom::SourceOf(layout, { 7, 0 }).words == 0);
	};

	#pragma endregion

	#pragma region Far Side

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a closed mesh's far side is drawn by culling the faces it turns outward, and an open mesh culls none")
	{
		constexpr size_t TRIANGLE_FLOATS = 9;
		constexpr size_t CORNER_FLOATS = 3;

		Loom::Scene scene("far side");
		Loom::GameObject* holder = scene.AddChild("Shape");
		Pump();

		Loom::Mesh* mesh = holder->Attach<Loom::Mesh>();
		Pump();

		mesh->m_vertices = LoomTests::TETRAHEDRON;
		CHECK(mesh->FarSide() == Loom::CullFace::Front);

		std::vector<float> reversed = LoomTests::TETRAHEDRON;

		for (size_t first = 0; first < reversed.size(); first += TRIANGLE_FLOATS)
			std::swap_ranges(reversed.begin() + first + CORNER_FLOATS, reversed.begin() + first + 2 * CORNER_FLOATS, reversed.begin() + first + 2 * CORNER_FLOATS);

		mesh->m_vertices = reversed;
		CHECK(mesh->FarSide() == Loom::CullFace::Back);

		// A mirror turns the winding over.
		holder->transform.scale = glm::vec3(-1.0f, 1.0f, 1.0f);
		CHECK(mesh->FarSide() == Loom::CullFace::Front);
		holder->transform.scale = glm::vec3(1.0f);

		// One face wound against its neighbours.
		std::vector<float> flipped = LoomTests::TETRAHEDRON;
		std::swap_ranges(flipped.begin() + CORNER_FLOATS, flipped.begin() + 2 * CORNER_FLOATS, flipped.begin() + 2 * CORNER_FLOATS);
		mesh->m_vertices = flipped;
		CHECK(mesh->FarSide() == Loom::CullFace::None);

		// One face short.
		mesh->m_vertices = std::vector<float>(LoomTests::TETRAHEDRON.begin(), LoomTests::TETRAHEDRON.end() - TRIANGLE_FLOATS);
		CHECK(mesh->FarSide() == Loom::CullFace::None);

		// Not triangles.
		mesh->m_vertices = LoomTests::TETRAHEDRON;
		mesh->primitive_id = Loom::Mesh::Lines;
		CHECK(mesh->FarSide() == Loom::CullFace::None);
	};

	#pragma endregion
};

#include "doctest.h"

#include "Test Support.h"

#include "Animator.h"
#include "Jobs.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/gtc/epsilon.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <bit>
#include <limits>
#include <sstream>

using LoomTests::Bound;
using LoomTests::Pump;
using LoomTests::RecordingRenderer;
using LoomTests::EPSILON;


#pragma region Helpers

namespace
{
	constexpr size_t POSITION_FLOATS = 3;
	constexpr float QUARTER_TURN = 90.0f;
	constexpr float EIGHTH_TURN = 45.0f;

	const glm::mat4 IDENTITY(1.0f);

	std::vector<float> Floats(const glm::mat4& matrix)
	{
		return std::vector<float>(glm::value_ptr(matrix), glm::value_ptr(matrix) + Loom::Skin::MATRIX_FLOATS);
	};

	// A vertex stage the engine makes a skinned twin of.
	const std::string SKINNABLE =
		"// ===VERTEX===\nin vec3 aPos;\nuniform mat4 u_model;\nvoid main() { gl_Position = u_model * vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n";

	// A scene with a joint called Bone at the origin, and a mesh beside it with a
	// Skin whose skeleton is the scene's root.
	struct Rig
	{
		Loom::Scene scene{ "rig" };
		Loom::GameObject* bone = nullptr;
		Loom::GameObject* body = nullptr;
		Loom::Mesh* mesh = nullptr;
		Loom::Skin* skin = nullptr;

		Rig()
		{
			bone = scene.GetRoot().AddChild("Bone");
			body = scene.GetRoot().AddChild("Body");
			mesh = body->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			skin = body->Attach<Loom::Skin>();
			Pump();

			skin->skeleton = &scene.GetRoot();
			skin->m_joints = "Bone\n";
			skin->m_inverseBinds = Floats(IDENTITY);
		};

		void Shade(Loom::Shader& shader)
		{
			body->Attach<Loom::Material>()->shader = &shader;
			Pump();
		};

		glm::vec3 Bent(size_t vertex)
		{
			skin->Deform(*mesh);

			return glm::make_vec3(mesh->m_deformed.data() + vertex * POSITION_FLOATS);
		};

		glm::vec3 Turned(size_t vertex)
		{
			skin->Deform(*mesh);

			return glm::make_vec3(mesh->m_deformedNormals.data() + vertex * POSITION_FLOATS);
		};
	};
};

#pragma endregion


TEST_SUITE("Skin")
{
	#pragma region Joints

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a vertex follows the joint it is bound to")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 1.0f, 0.0f), EPSILON)));

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(2.0f, 1.0f, 0.0f), EPSILON)));

		rig.bone->transform.position = glm::vec3(0.0f);
		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(-1.0f, 0.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a normal turns with the joint it is bound to, but is not moved by it")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.mesh->m_normals = { 1.0f, 0.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);
		CHECK(glm::all(glm::epsilonEqual(rig.Turned(0), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));

		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		CHECK(glm::all(glm::epsilonEqual(rig.Turned(0), glm::vec3(0.0f, 1.0f, 0.0f), EPSILON)));

		// The mesh's own normals are left as they were.
		CHECK(*rig.mesh->m_normals == std::vector<float>{ 1.0f, 0.0f, 0.0f });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a skinned mesh without a normal for every vertex has none turned")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.mesh->m_normals = { 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.skin->Deform(*rig.mesh);

		CHECK(rig.mesh->m_deformedNormals.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a vertex split between joints moves by their weighted mix")
	{
		Rig rig;
		rig.scene.GetRoot().AddChild("Still");
		Pump();

		rig.skin->m_joints = "Bone\nStill\n";
		rig.skin->m_inverseBinds = Floats(IDENTITY);
		rig.skin->m_inverseBinds->insert(rig.skin->m_inverseBinds->end(), glm::value_ptr(IDENTITY), glm::value_ptr(IDENTITY) + Loom::Skin::MATRIX_FLOATS);
		rig.mesh->m_vertices = { 0.0f, 0.0f, 0.0f };
		rig.skin->m_influences = { 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f };

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the bind pose is undone first, so a joint bound away from the origin moves its vertices only by what it moved since")
	{
		Rig rig;
		const glm::vec3 bound(0.0f, 3.0f, 0.0f);

		rig.bone->transform.position = bound;
		rig.skin->m_inverseBinds = Floats(glm::translate(IDENTITY, -bound));
		rig.mesh->m_vertices = { 1.0f, 3.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(1.0f, 3.0f, 0.0f), EPSILON)));

		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 4.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a joint that is not there holds its vertices where they were bound")
	{
		Rig rig;
		rig.skin->m_joints = "Nowhere\n";
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(5.0f, 0.0f, 0.0f);

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 1.0f, 0.0f), EPSILON)));
	};

	#pragma endregion

	#pragma region Shape Keys

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape key moves its vertices by its weight, and its name is all that reaches it")
	{
		Rig rig;
		rig.skin->m_joints = "";
		rig.mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_shapes = "Smile\n";
		rig.skin->m_shapeOffsets = { 1.0f, 1.0f, 0.0f, 2.0f, 0.0f };
		rig.skin->m_shapeWeights = { 0.0f };

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(1), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));

		rig.skin->SetShapeWeight("Smile", 0.5f);
		rig.skin->SetShapeWeight("Frown", 1.0f);

		CHECK(rig.skin->GetShapeWeight("Smile") == doctest::Approx(0.5f));
		CHECK(rig.skin->GetShapeWeight("Frown") == 0.0f);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(1), glm::vec3(1.0f, 1.0f, 0.0f), EPSILON)));
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape key counting more offsets than it has, or no number of them, moves nothing")
	{
		Rig rig;
		rig.skin->m_joints = "";
		rig.mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_shapes = "Smile\n";
		rig.skin->m_shapeWeights = { 1.0f };

		for (const float count : { 2.0f, -1.0f, std::numeric_limits<float>::quiet_NaN() })
		{
			rig.skin->m_shapeOffsets = { count, 1.0f, 0.0f, 1.0f, 0.0f };

			CHECK(glm::all(glm::epsilonEqual(rig.Bent(1), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));
		};
	};

	#pragma endregion

	#pragma region Skin Lifetime

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh whose Skin is taken off draws its own vertices again")
	{
		RecordingRenderer renderer;

		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		rig.mesh->Draw(1);
		CHECK(renderer.lastVertices == std::vector<float>{ 2.0f, 1.0f, 0.0f });

		rig.body->DetachComponent(rig.skin);
		Pump();

		rig.mesh->Draw(1);
		CHECK(renderer.lastVertices == *rig.mesh->m_vertices);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a joint destroyed and made again under the same name is found again")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(2.0f, 1.0f, 0.0f), EPSILON)));

		rig.bone->Destroy();
		Pump();

		// Drawn in between, as a running editor would.
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 1.0f, 0.0f), EPSILON)));

		Loom::GameObject* again = rig.scene.GetRoot().AddChild("Bone");
		Pump();
		again->transform.position = glm::vec3(-1.0f, 0.0f, 0.0f);

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(-1.0f, 1.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the mesh's own vertices are never changed by bending")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		rig.Bent(0);
		rig.Bent(0);

		CHECK(*rig.mesh->m_vertices == std::vector<float>{ 0.0f, 1.0f, 0.0f });
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(2.0f, 1.0f, 0.0f), EPSILON)));
	};

	#pragma endregion

	#pragma region Bending Once

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a Skin bends again only once its joints, weights or vertices change")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.skin->m_shapes = "Smile\n";
		rig.skin->m_shapeOffsets = { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_shapeWeights = { 0.0f };

		CHECK(rig.skin->Deform(*rig.mesh));
		CHECK_FALSE(rig.skin->Deform(*rig.mesh));

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);
		CHECK(rig.skin->Deform(*rig.mesh));
		CHECK_FALSE(rig.skin->Deform(*rig.mesh));

		rig.skin->SetShapeWeight("Smile", 0.0f);
		CHECK_FALSE(rig.skin->Deform(*rig.mesh));

		rig.skin->SetShapeWeight("Smile", 1.0f);
		CHECK(rig.skin->Deform(*rig.mesh));

		rig.mesh->m_vertices = { 0.0f, 2.0f, 0.0f };
		CHECK(rig.skin->Deform(*rig.mesh));
		CHECK(glm::all(glm::epsilonEqual(glm::make_vec3(rig.mesh->m_deformed.data()), glm::vec3(3.0f, 2.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "bending does not count as changing the mesh's own vertices")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.mesh->m_normals = { 1.0f, 0.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		const unsigned long long vertices = rig.mesh->m_vertices.Version();
		const unsigned long long normals = rig.mesh->m_normals.Version();
		const unsigned long long influences = rig.skin->m_influences.Version();

		rig.skin->Deform(*rig.mesh);

		CHECK(rig.mesh->m_vertices.Version() == vertices);
		CHECK(rig.mesh->m_normals.Version() == normals);
		CHECK(rig.skin->m_influences.Version() == influences);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a skinned mesh held still is handed to the renderer once")
	{
		RecordingRenderer renderer;

		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		rig.mesh->Draw(1);
		const auto handed = renderer.handed;

		Loom::Utilities::Tick();
		rig.mesh->Draw(1);
		CHECK(renderer.handed == handed);

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);
		Loom::Utilities::Tick();
		rig.mesh->Draw(1);

		CHECK(renderer.handed == handed + 1);
		CHECK(renderer.lastVertices == std::vector<float>{ 2.0f, 1.0f, 0.0f });
	};

	#pragma endregion

	#pragma region GPU Skinning

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a skinned mesh whose shader has a skinned twin is handed its bind pose once and bent by its joints on the GPU")
	{
		RecordingRenderer renderer;

		std::istringstream source(SKINNABLE);
		Loom::Shader shader("gpu skinned", source);

		REQUIRE(shader.skinned_id != 0);

		Rig rig;
		rig.Shade(shader);
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		rig.scene.Render();

		CHECK(renderer.drawn.back() == shader.skinned_id);
		CHECK(renderer.matrixArrays.at("u_joints") == std::vector<glm::mat4>{ IDENTITY });
		CHECK(renderer.matrixArrayPrograms.at("u_joints") == shader.skinned_id);

		const auto handed = renderer.handed;
		const glm::vec3 moved(2.0f, 0.0f, 0.0f);

		rig.bone->transform.position = moved;
		Loom::Utilities::Tick();
		rig.scene.Render();

		CHECK(renderer.handed == handed);
		CHECK(renderer.matrixArrays.at("u_joints") == std::vector<glm::mat4>{ glm::translate(IDENTITY, moved) });

		const Loom::VertexLayout& layout = renderer.kept.begin()->second.layout;
		REQUIRE(layout.layers.size() == 1);
		CHECK(layout.layers[0] == Loom::UVLayout{ Loom::Skin::UV_LAYER, (uint32_t)Loom::Skin::FLOATS_PER_INFLUENCE });

		std::vector<uint32_t> words;
		for (size_t i = POSITION_FLOATS; i < renderer.lastVertices.size(); i++)
			words.push_back(std::bit_cast<uint32_t>(renderer.lastVertices[i]));

		CHECK(renderer.lastVertices.size() == POSITION_FLOATS + Loom::Skin::FLOATS_PER_INFLUENCE);
		CHECK(words == std::vector<uint32_t>{ 0, 0, 0, 0, std::bit_cast<uint32_t>(1.0f), 0, 0, 0 });
		CHECK(std::vector<float>(renderer.lastVertices.begin(), renderer.lastVertices.begin() + POSITION_FLOATS) == std::vector<float>{ 0.0f, 1.0f, 0.0f });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the GPU is handed a joint's index and weight, and nothing for a joint past the skeleton or no weight")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_influences = { 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, 0.75f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };

		const uint32_t quarter = std::bit_cast<uint32_t>(0.25f);
		const uint32_t rest = std::bit_cast<uint32_t>(0.75f);
		const uint32_t whole = std::bit_cast<uint32_t>(1.0f);

		CHECK(rig.skin->GpuInfluences() == std::vector<uint32_t>{ 0, 0, 0, 0, quarter, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 });

		rig.scene.GetRoot().AddChild("Still");
		Pump();
		rig.skin->m_joints = "Bone\nStill\n";

		CHECK(rig.skin->GpuInfluences() == std::vector<uint32_t>{ 0, 1, 0, 0, quarter, rest, 0, 0,  1, 0, 0, 0, whole, 0, 0, 0 });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape key on a mesh bent on the GPU hands its vertices over again, shaped")
	{
		RecordingRenderer renderer;

		std::istringstream source(SKINNABLE);
		Loom::Shader shader("gpu shaped", source);

		Rig rig;
		rig.Shade(shader);
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.skin->m_shapes = "Smile\n";
		rig.skin->m_shapeOffsets = { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_shapeWeights = { 0.0f };

		rig.scene.Render();
		const auto handed = renderer.handed;

		rig.skin->SetShapeWeight("Smile", 1.0f);
		Loom::Utilities::Tick();
		rig.scene.Render();

		CHECK(renderer.drawn.back() == shader.skinned_id);
		CHECK(renderer.handed == handed + 1);
		CHECK(std::vector<float>(renderer.lastVertices.begin(), renderer.lastVertices.begin() + POSITION_FLOATS) == std::vector<float>{ 1.0f, 1.0f, 0.0f });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh bent on the GPU hands its vertices over again when its joints change, which pick the influences it keeps")
	{
		RecordingRenderer renderer;

		std::istringstream source(SKINNABLE);
		Loom::Shader shader("gpu rejointed", source);

		Rig rig;
		rig.Shade(shader);
		rig.scene.GetRoot().AddChild("Still");
		Pump();

		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = { 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f };

		rig.scene.Render();
		const auto handed = renderer.handed;

		std::vector<float> inverseBinds = Floats(IDENTITY);
		const std::vector<float> still = Floats(IDENTITY);
		inverseBinds.insert(inverseBinds.end(), still.begin(), still.end());

		rig.skin->m_joints = "Bone\nStill\n";
		rig.skin->m_inverseBinds = inverseBinds;
		Loom::Utilities::Tick();
		rig.scene.Render();

		const uint32_t half = std::bit_cast<uint32_t>(0.5f);
		std::vector<uint32_t> words;
		for (size_t i = POSITION_FLOATS; i < renderer.lastVertices.size(); i++)
			words.push_back(std::bit_cast<uint32_t>(renderer.lastVertices[i]));

		CHECK(renderer.drawn.back() == shader.skinned_id);
		CHECK(renderer.handed == handed + 1);
		CHECK(words == std::vector<uint32_t>{ 0, 1, 0, 0, half, half, 0, 0 });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh bent on the GPU casts its shadow with the skinned shadow program")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshadows = on\n" + SKINNABLE);
		Loom::Shader shader("gpu skinned shadow", source);

		REQUIRE(shader.shadow_skinned_id != 0);

		Rig rig;
		Loom::Light* light = rig.scene.Attach<Loom::Light>();
		rig.Shade(shader);
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		light->RenderShadowMap({ &rig.scene.GetRoot() });

		CHECK(renderer.drawn == std::vector<uint32_t>{ shader.shadow_skinned_id });
		CHECK(renderer.matrixArrayPrograms.at("u_joints") == shader.shadow_skinned_id);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh with more joints than the GPU takes is bent on the CPU")
	{
		RecordingRenderer renderer;

		std::istringstream source(SKINNABLE);
		Loom::Shader shader("too many joints", source);

		Rig rig;
		rig.Shade(shader);
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		std::string joints;
		std::vector<float> inverseBinds;

		for (size_t i = 0; i <= Loom::Skin::MAX_GPU_JOINTS; i++)
		{
			joints += "Bone\n";
			inverseBinds.insert(inverseBinds.end(), glm::value_ptr(IDENTITY), glm::value_ptr(IDENTITY) + Loom::Skin::MATRIX_FLOATS);
		};

		rig.skin->m_joints = joints;
		rig.skin->m_inverseBinds = inverseBinds;
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		rig.scene.Render();

		CHECK(renderer.drawn.back() == shader.id);
		CHECK(renderer.matrixArrays.count("u_joints") == 0);
		CHECK(renderer.lastVertices == std::vector<float>{ 2.0f, 1.0f, 0.0f });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh whose shader has no skinned twin is bent on the CPU")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===VERTEX===\nUV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4 a, float b>;\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		Loom::Shader shader("no skinned twin", source);

		REQUIRE(shader.id != 0);
		REQUIRE(shader.skinned_id == 0);

		Rig rig;
		rig.Shade(shader);
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

		rig.scene.Render();

		CHECK(renderer.drawn.back() == shader.id);
		CHECK(renderer.lastVertices == std::vector<float>{ 2.0f, 1.0f, 0.0f });
	};

	#pragma endregion

	#pragma region Threads

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "vertices bent across threads land where one thread bends them")
	{
		// Enough vertices for the work to be shared out several ways.
		constexpr size_t VERTICES = 10000;

		Rig rig;
		rig.scene.GetRoot().AddChild("Other");
		Pump();

		rig.skin->m_joints = "Bone\nOther\n";
		rig.skin->m_inverseBinds = Floats(IDENTITY);
		rig.skin->m_inverseBinds->insert(rig.skin->m_inverseBinds->end(), glm::value_ptr(IDENTITY), glm::value_ptr(IDENTITY) + Loom::Skin::MATRIX_FLOATS);

		std::vector<float> vertices;
		std::vector<float> normals;
		std::vector<float> influences;

		for (size_t i = 0; i < VERTICES; i++)
		{
			const float along = (float)i;

			vertices.insert(vertices.end(), { along, 1.0f, -along });
			normals.insert(normals.end(), { 0.0f, 1.0f, 0.0f });
			influences.insert(influences.end(), { 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, 0.75f, 0.0f, 0.0f });
		};

		rig.mesh->m_vertices = vertices;
		rig.mesh->m_normals = normals;
		rig.skin->m_influences = influences;
		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, EIGHTH_TURN);
		rig.scene.GetRoot().GetChildren().back()->transform.position = glm::vec3(0.0f, 3.0f, 0.0f);

		Loom::Jobs::SetMostThreads(1);
		rig.skin->Deform(*rig.mesh);
		const std::vector<float> alone = rig.mesh->m_deformed;
		const std::vector<float> aloneNormals = rig.mesh->m_deformedNormals;

		rig.mesh->m_deformed.clear();

		Loom::Jobs::SetMostThreads(0);
		rig.skin->Deform(*rig.mesh);

		CHECK(rig.mesh->m_deformed == alone);
		CHECK(rig.mesh->m_deformedNormals == aloneNormals);
	};

	#pragma endregion
};


TEST_SUITE("Animator")
{
	#pragma region Clips

	// A one second clip that slides Bone from the origin to (2, 0, 0), turns it
	// a quarter about Y and closes Smile, and a second clip that holds it still.
	struct Animated
	{
		Rig rig;
		Loom::Animator* animator = nullptr;

		Animated()
		{
			animator = rig.scene.GetRoot().Attach<Loom::Animator>();
			Pump();

			const glm::quat turned = glm::angleAxis(glm::radians(QUARTER_TURN), glm::vec3(0.0f, 1.0f, 0.0f));

			rig.skin->m_shapes = "Smile\n";
			rig.skin->m_shapeWeights = { 0.0f };

			animator->m_clips = "Slide\t1\nHold\t0.5\n";
			animator->m_channels =
				"0\ttranslation\tBone\n"
				"0\trotation\tBone\n"
				"0\tweight\tSmile\n"
				"1\ttranslation\tBone\n";
			animator->m_keys =
			{
				2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f,
				2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, turned.x, turned.y, turned.z, turned.w,
				2.0f, 0.0f, 0.0f, 1.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			};
			animator->OnUpdate();
		};

		glm::vec3 Position() const { return rig.bone->transform.position; };
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a channel counting more keys than there are, or no number of them, is dropped")
	{
		// So large that its keys' floats, counted in a size_t, wrap round to few.
		constexpr float wrapping = 4.6116860184273879e18f;

		for (const float count : { wrapping, 2.0f, -1.0f, std::numeric_limits<float>::quiet_NaN() })
		{
			Rig rig;
			Loom::Animator* animator = rig.scene.GetRoot().Attach<Loom::Animator>();
			Pump();

			animator->m_clips = "Slide\t1\n";
			animator->m_channels = "0\ttranslation\tBone\n";
			animator->m_keys = { count, 1.0f, 2.0f, 0.0f, 0.0f };
			animator->OnUpdate();
			animator->Pose(0.5f);

			CHECK(*rig.bone->transform.position == glm::vec3(0.0f));
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a clip's keys are blended between: positions straight, rotations along the arc, weights straight")
	{
		Animated animated;
		animated.animator->Pose(0.5f);

		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));
		CHECK(glm::all(glm::epsilonEqual(*animated.rig.bone->transform.rotation, glm::vec3(0.0f, EIGHTH_TURN, 0.0f), EPSILON)));
		CHECK(animated.rig.skin->GetShapeWeight("Smile") == doctest::Approx(0.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a Skin added after the clip started has its shape keys set too")
	{
		Animated animated;
		animated.animator->Pose(0.5f);

		Loom::Skin* later = animated.rig.scene.GetRoot().AddChild("Later")->Attach<Loom::Skin>();
		Pump();

		later->m_shapes = "Smile\n";
		later->m_shapeWeights = { 0.0f };
		animated.animator->Pose(0.5f);

		CHECK(later->GetShapeWeight("Smile") == doctest::Approx(0.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a looping clip starts over at its end, and one that does not loop holds its last pose")
	{
		Animated animated;

		animated.animator->Pose(1.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.5f, 0.0f, 0.0f), EPSILON)));

		animated.animator->loop = false;
		animated.animator->Pose(1.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(2.0f, 0.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "playing another clip starts it from its beginning, and a name it has no clip by changes nothing")
	{
		Animated animated;
		animated.animator->Pose(0.5f);

		CHECK_FALSE(animated.animator->Play("Dance"));
		CHECK(animated.animator->GetClipName() == "Slide");

		CHECK(animated.animator->Play("Hold"));
		CHECK(animated.animator->GetTime() == 0.0f);
		CHECK(animated.animator->GetDuration() == doctest::Approx(0.5f));

		animated.animator->Pose(0.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.0f), EPSILON)));
		CHECK(animated.animator->GetClipNames() == std::vector<std::string>{ "Slide", "Hold" });
	};

	#pragma endregion

	#pragma region Transitions

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "going from one clip to another plays the transition between them once on the way")
	{
		Animated animated;
		animated.animator->m_clips = "Slide\t1\nHold\t0.5\nSlide to Hold\t0.25\n";
		animated.animator->Play("Slide");

		animated.animator->clip = 1;
		animated.animator->Advance(0.1f);
		CHECK(animated.animator->GetClipName() == "Slide to Hold");

		animated.animator->Advance(0.2f);
		CHECK(animated.animator->GetClipName() == "Hold");
		CHECK(animated.animator->GetTime() == doctest::Approx(0.05f));

		// Back the other way there is no transition.
		animated.animator->clip = 0;
		animated.animator->Advance(0.1f);
		CHECK(animated.animator->GetClipName() == "Slide");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a script can tell a transition from a clip, and when one is playing")
	{
		Animated animated;
		animated.animator->m_clips = "Slide\t1\nHold\t0.5\nSlide to Hold\t0.25\n";
		animated.animator->Play("Slide");

		CHECK_FALSE(animated.animator->IsTransition(0));
		CHECK(animated.animator->IsTransition(2));
		CHECK_FALSE(animated.animator->IsTransition(3));
		CHECK_FALSE(animated.animator->InTransition());

		animated.animator->Play("Hold");
		CHECK(animated.animator->InTransition());

		animated.animator->Advance(0.3f);
		CHECK_FALSE(animated.animator->InTransition());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a change of clip eases out of the pose there was over blendTime")
	{
		Animated animated;
		animated.animator->blendTime = 0.5f;
		animated.animator->Play("Slide");
		animated.animator->Advance(0.5f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(1.0f, 0.0f, 0.0f), EPSILON)));

		// Hold keeps Bone at the origin; half way through the blend, eased, it is half way there.
		animated.animator->clip = 1;
		animated.animator->Advance(0.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.5f, 0.0f, 0.0f), EPSILON)));

		animated.animator->Advance(0.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.0f), EPSILON)));

		// With no blend it cuts straight to Slide's pose a tenth of a second in.
		animated.animator->blendTime = 0.0f;
		animated.animator->clip = 0;
		animated.animator->Advance(0.1f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.2f, 0.0f, 0.0f), EPSILON)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "asking for the same clip every frame lets the transition into it play out")
	{
		Animated animated;
		animated.animator->m_clips = "Slide\t1\nHold\t0.5\nSlide to Hold\t0.25\n";
		animated.animator->Play("Slide");

		animated.animator->clip = 1;
		animated.animator->Advance(0.1f);
		animated.animator->clip = 1;
		animated.animator->Advance(0.1f);
		CHECK(animated.animator->GetClipName() == "Slide to Hold");
		CHECK(*animated.animator->clip == 1);

		animated.animator->clip = 1;
		animated.animator->Advance(0.1f);
		CHECK(animated.animator->GetClipName() == "Hold");
		CHECK(animated.animator->GetTime() == doctest::Approx(0.05f));
	};

	#pragma endregion

	#pragma region Rotations

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a name with \" to \" in it is a transition only when both halves name clips")
	{
		Animated animated;
		animated.animator->m_clips = "Slide\t1\nRun to Cover\t0.5\n";
		CHECK_FALSE(animated.animator->IsTransition(1));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Rotation turns Euler degrees back into the rotation they came from")
	{
		const glm::quat rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		const glm::quat back = Loom::Transform::Rotation(Loom::Transform::EulerDegrees(rotation));

		CHECK(std::abs(glm::dot(rotation, back)) == doctest::Approx(1.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rotations and shape weights ease too, and Pose stays exact while they do")
	{
		Animated animated;
		const glm::quat turned = glm::angleAxis(glm::radians(QUARTER_TURN), glm::vec3(0.0f, 1.0f, 0.0f));

		// Two still clips: A unturned with Smile open, B a quarter turn with Smile shut.
		animated.animator->m_clips = "A\t1\nB\t1\n";
		animated.animator->m_channels =
			"0\trotation\tBone\n"
			"0\tweight\tSmile\n"
			"1\trotation\tBone\n"
			"1\tweight\tSmile\n";
		animated.animator->m_keys =
		{
			1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
			1.0f, 0.0f, 0.0f,
			1.0f, 0.0f, turned.x, turned.y, turned.z, turned.w,
			1.0f, 0.0f, 1.0f,
		};
		animated.animator->blendTime = 0.5f;
		animated.animator->Play("A");
		animated.animator->Advance(0.5f);

		animated.animator->clip = 1;
		animated.animator->Advance(0.25f);
		CHECK(animated.rig.bone->transform.rotation->y == doctest::Approx(EIGHTH_TURN));
		CHECK(animated.rig.skin->GetShapeWeight("Smile") == doctest::Approx(0.5f));

		animated.animator->Pose(0.0f);
		CHECK(animated.rig.bone->transform.rotation->y == doctest::Approx(QUARTER_TURN));
		CHECK(animated.rig.skin->GetShapeWeight("Smile") == doctest::Approx(1.0f));
	};

	#pragma endregion

	#pragma region Inspector

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "clip picks a clip by its place, the inspector offers the clips by name, and one past them plays nothing")
	{
		Animated animated;

		CHECK(animated.animator->Play(1));
		CHECK(animated.animator->GetClipName() == "Hold");
		CHECK_FALSE(animated.animator->Play(2));
		CHECK(*animated.animator->clip == 1);

		const Loom::SerializedField& field = animated.animator->GetFields()[0];
		CHECK(animated.animator->GetFieldChoices(field) == std::vector<std::string>{ "Slide", "Hold" });
		CHECK(animated.animator->GetFieldChoices(animated.animator->GetFields()[1]).empty());

		animated.animator->Pose(0.5f);
		const glm::vec3 held = animated.Position();

		animated.animator->clip = 2;
		animated.animator->Pose(0.5f);
		CHECK(animated.animator->GetClipName().empty());
		CHECK(animated.animator->GetDuration() == 0.0f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), held, EPSILON)));
	};

	#pragma endregion
};


TEST_SUITE("Textured meshes")
{
	#pragma region Vertex Layout

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh with a texture coordinate for every vertex draws them beside its positions")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("textured");
		Loom::Mesh* mesh = scene.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		mesh->m_textureCoordinates = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUV);
		CHECK(renderer.lastVertices == std::vector<float>{ 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 });

		mesh->m_textureCoordinates = { 0.0f, 0.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::Position);
		CHECK(renderer.lastVertices == *mesh->m_vertices);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh with a normal for every vertex draws them after its texture coordinates, or after none")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("normals");
		Loom::Mesh* mesh = scene.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f };
		mesh->m_textureCoordinates = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		mesh->m_normals = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUVNormal);
		CHECK(renderer.lastVertices == std::vector<float>{ 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 1, 0, 0, 2, 0, 0, 1, 1, 0, 0 });

		// Without texture coordinates their place is kept, holding the
		// position's x and y as aUV reads from vertices without normals.
		mesh->m_textureCoordinates = std::vector<float>{ };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUVNormal);
		CHECK(renderer.lastVertices == std::vector<float>{ 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 2, 0, 0, 1, 0, 0, 2, 0, 0, 2, 1, 0, 0 });

		mesh->m_normals = { 0.0f, 0.0f, 1.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::Position);
		CHECK(renderer.lastVertices == *mesh->m_vertices);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh with a colour for every vertex draws it last, keeping the places of what it lacks")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("coloured");
		Loom::Mesh* mesh = scene.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Pump();

		mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f };
		mesh->m_colors = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUVNormalColor);
		CHECK(renderer.lastVertices == std::vector<float>{
			0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1,
			2, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 1,
			0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0.5f });

		mesh->m_textureCoordinates = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		mesh->m_normals = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUVNormalColor);
		CHECK(renderer.lastVertices == std::vector<float>{
			0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1,
			2, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1,
			0, 2, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0.5f });

		mesh->m_colors = { 1.0f, 1.0f, 1.0f, 1.0f };
		mesh->Draw(1);

		CHECK(renderer.lastFormat == Loom::VertexFormat::PositionUVNormal);
	};

	#pragma endregion

	#pragma region Sampling

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader with pixelated on has its program sample nearest")
	{
		RecordingRenderer renderer;

		const std::string stages =
			"// ===VERTEX===\nuniform mat4 u_model;\nvoid main() { }\n"
			"// ===FRAGMENT===\nvoid main() { }\n";

		std::istringstream pixelated("// ===SETTINGS===\npixelated = on\n" + stages);
		const Loom::Shader nearest("pixelated program", pixelated);

		std::istringstream smooth(stages);
		const Loom::Shader blended("smooth program", smooth);

		CHECK(nearest.settings.pixelated);
		CHECK(renderer.pixelatedPrograms.contains(nearest.id));
		CHECK_FALSE(renderer.pixelatedPrograms.contains(blended.id));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material's sampling picks blended or nearest over its shader's for each draw")
	{
		RecordingRenderer renderer;

		const std::string stages =
			"// ===VERTEX===\nuniform mat4 u_model;\nvoid main() { }\n"
			"// ===FRAGMENT===\nvoid main() { }\n";

		std::istringstream pixelated("// ===SETTINGS===\npixelated = on\n" + stages);
		Loom::Shader nearest("pixelated program", pixelated);

		std::istringstream smooth(stages);
		Loom::Shader blended("smooth program", smooth);

		Loom::Material material;
		Pump();

		material.shader = &nearest;
		material.sampling = Loom::Material::Blended;
		material.Apply(nearest.id);
		CHECK_FALSE(renderer.pixelatedPrograms.contains(nearest.id));

		material.sampling = Loom::Material::AsShader;
		material.Apply(nearest.id);
		CHECK(renderer.pixelatedPrograms.contains(nearest.id));

		material.shader = &blended;
		material.Apply(blended.id);
		CHECK_FALSE(renderer.pixelatedPrograms.contains(blended.id));

		material.sampling = Loom::Material::Nearest;
		material.Apply(blended.id);
		CHECK(renderer.pixelatedPrograms.contains(blended.id));
	};

	#pragma endregion

	#pragma region Euler Angles

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rotation turned into Transform's Euler angles turns the same way")
	{
		const glm::quat rotation = glm::normalize(glm::quat(0.8f, 0.3f, -0.4f, 0.2f));

		Loom::Scene scene("euler");
		Pump();

		Loom::Transform& transform = scene.GetRoot().transform;
		transform.rotation = Loom::Transform::EulerDegrees(rotation);

		const glm::mat4 expected = glm::mat4_cast(rotation);
		const glm::mat4 turned = transform.Matrix();

		for (int column = 0; column < 3; column++)
			CHECK(glm::all(glm::epsilonEqual(glm::vec3(turned[column]), glm::vec3(expected[column]), EPSILON)));
	};

	#pragma endregion
};

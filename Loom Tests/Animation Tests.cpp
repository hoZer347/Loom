#include "doctest.h"

#include "Test Support.h"

#include "Animator.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"
#include "Skin.h"

#include "glm/gtc/epsilon.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <sstream>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


namespace
{
	constexpr float CLOSE = 1e-4f;
	constexpr float QUARTER_TURN = 90.0f;
	constexpr float EIGHTH_TURN = 45.0f;

	const glm::mat4 IDENTITY(1.0f);

	std::vector<float> Floats(const glm::mat4& matrix)
	{
		return std::vector<float>(glm::value_ptr(matrix), glm::value_ptr(matrix) + Loom::Skin::MATRIX_FLOATS);
	};

	// One joint per vertex, all of its weight.
	std::vector<float> Bound(std::initializer_list<float> joints)
	{
		std::vector<float> influences;

		for (const float joint : joints)
			influences.insert(influences.end(), { joint, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f });

		return influences;
	};

	// A scene with a joint called Bone at the origin, and a mesh beside it with a
	// Skin whose skeleton is the scene's root.
	struct Rig
	{
		Loom::Scene scene{ "rig" };
		Loom::GameObject* bone = nullptr;
		Loom::Mesh* mesh = nullptr;
		Loom::Skin* skin = nullptr;

		Rig()
		{
			bone = scene.GetRoot().AddChild("Bone");
			Loom::GameObject* body = scene.GetRoot().AddChild("Body");
			mesh = body->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			skin = body->Attach<Loom::Skin>();
			Pump();

			skin->skeleton = &scene.GetRoot();
			skin->m_joints = "Bone\n";
			skin->m_inverseBinds = Floats(IDENTITY);
		};

		glm::vec3 Bent(size_t vertex)
		{
			skin->Deform(*mesh);

			return glm::make_vec3(mesh->m_deformed.data() + vertex * 3);
		};
	};
};


TEST_SUITE("Skin")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a vertex follows the joint it is bound to")
	{
		Rig rig;
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 1.0f, 0.0f), CLOSE)));

		rig.bone->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(2.0f, 1.0f, 0.0f), CLOSE)));

		rig.bone->transform.position = glm::vec3(0.0f);
		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(-1.0f, 0.0f, 0.0f), CLOSE)));
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

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(1.0f, 0.0f, 0.0f), CLOSE)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the bind pose is undone first, so a joint bound away from the origin moves its vertices only by what it moved since")
	{
		Rig rig;
		const glm::vec3 bound(0.0f, 3.0f, 0.0f);

		rig.bone->transform.position = bound;
		rig.skin->m_inverseBinds = Floats(glm::translate(IDENTITY, -bound));
		rig.mesh->m_vertices = { 1.0f, 3.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(1.0f, 3.0f, 0.0f), CLOSE)));

		rig.bone->transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 4.0f, 0.0f), CLOSE)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a joint that is not there holds its vertices where they were bound")
	{
		Rig rig;
		rig.skin->m_joints = "Nowhere\n";
		rig.mesh->m_vertices = { 0.0f, 1.0f, 0.0f };
		rig.skin->m_influences = Bound({ 0.0f });
		rig.bone->transform.position = glm::vec3(5.0f, 0.0f, 0.0f);

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f, 1.0f, 0.0f), CLOSE)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shape key moves its vertices by its weight, and its name is all that reaches it")
	{
		Rig rig;
		rig.skin->m_joints = "";
		rig.mesh->m_vertices = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		rig.skin->m_shapes = "Smile\n";
		rig.skin->m_shapeOffsets = { 1.0f, 1.0f, 0.0f, 2.0f, 0.0f };
		rig.skin->m_shapeWeights = { 0.0f };

		CHECK(glm::all(glm::epsilonEqual(rig.Bent(1), glm::vec3(1.0f, 0.0f, 0.0f), CLOSE)));

		rig.skin->SetShapeWeight("Smile", 0.5f);
		rig.skin->SetShapeWeight("Frown", 1.0f);

		CHECK(rig.skin->GetShapeWeight("Smile") == doctest::Approx(0.5f));
		CHECK(rig.skin->GetShapeWeight("Frown") == 0.0f);
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(1), glm::vec3(1.0f, 1.0f, 0.0f), CLOSE)));
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(0.0f), CLOSE)));
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
		CHECK(glm::all(glm::epsilonEqual(rig.Bent(0), glm::vec3(2.0f, 1.0f, 0.0f), CLOSE)));
	};
};


TEST_SUITE("Animator")
{
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
			animator->clip = "Slide";
			animator->OnUpdate();
		};

		glm::vec3 Position() const { return rig.bone->transform.position; };
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a clip's keys are blended between: positions straight, rotations along the arc, weights straight")
	{
		Animated animated;
		animated.animator->Pose(0.5f);

		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(1.0f, 0.0f, 0.0f), CLOSE)));
		CHECK(glm::all(glm::epsilonEqual(*animated.rig.bone->transform.rotation, glm::vec3(0.0f, EIGHTH_TURN, 0.0f), CLOSE)));
		CHECK(animated.rig.skin->GetShapeWeight("Smile") == doctest::Approx(0.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a looping clip starts over at its end, and one that does not loop holds its last pose")
	{
		Animated animated;

		animated.animator->Pose(1.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.5f, 0.0f, 0.0f), CLOSE)));

		animated.animator->loop = false;
		animated.animator->Pose(1.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(2.0f, 0.0f, 0.0f), CLOSE)));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "playing another clip starts it from its beginning, and a name it has no clip by changes nothing")
	{
		Animated animated;
		animated.animator->Pose(0.5f);

		CHECK_FALSE(animated.animator->Play("Dance"));
		CHECK(*animated.animator->clip == "Slide");

		CHECK(animated.animator->Play("Hold"));
		CHECK(animated.animator->GetTime() == 0.0f);
		CHECK(animated.animator->GetDuration() == doctest::Approx(0.5f));

		animated.animator->Pose(0.25f);
		CHECK(glm::all(glm::epsilonEqual(animated.Position(), glm::vec3(0.0f), CLOSE)));
		CHECK(animated.animator->GetClipNames() == std::vector<std::string>{ "Slide", "Hold" });
	};
};


TEST_SUITE("Textured meshes")
{
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
			CHECK(glm::all(glm::epsilonEqual(glm::vec3(turned[column]), glm::vec3(expected[column]), CLOSE)));
	};
};

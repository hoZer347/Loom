#include "doctest.h"

#include "Test Support.h"

#include "GameObject.h"
#include "Jobs.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using LoomTests::Pump;


#pragma region Helpers

namespace
{
	// About the vertices of RTS Main's figure, on a humanoid rig's joints.
	constexpr size_t VERTICES = 100000;
	constexpr size_t JOINTS = 64;

	// The quickest of several tries, so a thread switched out mid-bend costs only a retry.
	constexpr int TRIES = 5;

	constexpr size_t POSITION_FLOATS = 3;
	constexpr double MICROSECONDS_PER_MILLISECOND = 1000.0;

	// VERTICES each bound to four of the JOINTS under the scene's root, as a model's figure is.
	struct Figure
	{
		Loom::Scene scene{ "figure" };
		std::vector<Loom::GameObject*> joints;
		Loom::Mesh* mesh = nullptr;
		Loom::Skin* skin = nullptr;

		Figure()
		{
			std::string names;
			std::vector<float> inverseBinds;
			const glm::mat4 identity(1.0f);

			for (size_t i = 0; i < JOINTS; i++)
			{
				const std::string name = "Joint " + std::to_string(i);

				joints.push_back(scene.GetRoot().AddChild(name));
				names += name + "\n";
				inverseBinds.insert(inverseBinds.end(), glm::value_ptr(identity), glm::value_ptr(identity) + Loom::Skin::MATRIX_FLOATS);
			};

			Loom::GameObject* body = scene.GetRoot().AddChild("Body");
			mesh = body->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			skin = body->Attach<Loom::Skin>();
			Pump();

			std::vector<float> vertices;
			std::vector<float> normals;
			std::vector<float> influences;

			for (size_t v = 0; v < VERTICES; v++)
			{
				const float along = (float)v / (float)VERTICES;
				const float first = (float)(v % JOINTS);

				vertices.insert(vertices.end(), { along, 1.0f - along, along * along });
				normals.insert(normals.end(), { 0.0f, 1.0f, 0.0f });
				influences.insert(influences.end(), { first, (float)((v + 1) % JOINTS), (float)((v + 2) % JOINTS), (float)((v + 3) % JOINTS), 0.4f, 0.3f, 0.2f, 0.1f });
			};

			mesh->m_vertices = vertices;
			mesh->m_normals = normals;
			skin->skeleton = &scene.GetRoot();
			skin->m_joints = names;
			skin->m_inverseBinds = inverseBinds;
			skin->m_influences = influences;
		};

		// Moves every joint a little, as an animation does each frame.
		void Move()
		{
			for (Loom::GameObject* joint : joints)
				joint->transform.position = *std::as_const(joint->transform.position) + glm::vec3(0.0f, 0.001f, 0.0f);
		};

		// The quickest of TRIES bends in milliseconds, each after work moves the joints or not.
		template <typename Work>
		double Quickest(const Work& work)
		{
			double quickest = std::numeric_limits<double>::max();

			for (int i = 0; i < TRIES; i++)
			{
				work();

				const auto start = std::chrono::steady_clock::now();
				skin->Deform(*mesh);
				const auto took = std::chrono::steady_clock::now() - start;

				quickest = std::min(quickest, std::chrono::duration<double, std::micro>(took).count() / MICROSECONDS_PER_MILLISECOND);
			};

			return quickest;
		};
	};
};

#pragma endregion


TEST_SUITE("Performance" * doctest::skip())
{
	#pragma region Skinning

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a skinned figure bends across every core, and not at all while it holds still")
	{
		// On this many cores or more, bending must take a third of one core's time or less.
		constexpr unsigned SHARED_CORES = 4;
		constexpr double LEAST_SHARED_GAIN = 3.0;

		// Holding still only finds the joints, so it must cost a fiftieth of a bend or less.
		constexpr double LEAST_STILL_GAIN = 50.0;

		Figure figure;

		Loom::Jobs::SetMostThreads(1);
		const double alone = figure.Quickest([&]() { figure.Move(); });

		Loom::Jobs::SetMostThreads(0);
		const double shared = figure.Quickest([&]() { figure.Move(); });
		const double still = figure.Quickest([]() { });

		std::ostringstream report;
		report << VERTICES << " vertices on " << JOINTS << " joints: bent in " << alone << " ms on one core, "
			<< shared << " ms on " << Loom::Jobs::Threads() << ", held still in " << still << " ms";
		MESSAGE(report.str());

		if (Loom::Jobs::Threads() >= SHARED_CORES)
			CHECK(alone / shared >= LEAST_SHARED_GAIN);

		CHECK(shared / still >= LEAST_STILL_GAIN);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a moving figure the GPU bends is handed over once, and drawn far quicker than one bent on the CPU")
	{
		// Only joints go to the GPU each frame, so it must take a tenth of a CPU frame or less.
		constexpr double LEAST_GPU_GAIN = 10.0;

		LoomTests::RecordingRenderer renderer;
		renderer.recordVertices = false;

		std::istringstream source(
			"// ===VERTEX===\nin vec3 aPos;\nuniform mat4 u_model;\nvoid main() { gl_Position = u_model * vec4(aPos, 1.0); }\n"
			"// ===FRAGMENT===\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n");
		Loom::Shader skinnable("gpu figure", source);

		std::istringstream unskinnable_source(
			"// ===VERTEX===\nUV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4 a, float b>;\nvoid main() { }\n"
			"// ===FRAGMENT===\nvoid main() { }\n");
		Loom::Shader unskinnable("cpu figure", unskinnable_source);

		REQUIRE(skinnable.skinned_id != 0);
		REQUIRE(unskinnable.skinned_id == 0);

		Figure figure;
		Loom::Material* material = figure.mesh->GetGameObject()->Attach<Loom::Material>();
		Pump();

		const auto frame = [&]()
		{
			double quickest = std::numeric_limits<double>::max();

			for (int i = 0; i < TRIES; i++)
			{
				figure.Move();
				Loom::Utilities::Tick();

				const auto start = std::chrono::steady_clock::now();
				figure.scene.Render();
				const auto took = std::chrono::steady_clock::now() - start;

				quickest = std::min(quickest, std::chrono::duration<double, std::micro>(took).count() / MICROSECONDS_PER_MILLISECOND);
			};

			return quickest;
		};

		material->shader = &unskinnable;
		const double cpu = frame();

		material->shader = &skinnable;
		Loom::Utilities::Tick();
		figure.scene.Render();
		const size_t handed = renderer.handed;
		const double gpu = frame();

		std::ostringstream report;
		report << VERTICES << " vertices on " << JOINTS << " joints: a moving frame takes " << cpu << " ms bent on the CPU, "
			<< gpu << " ms bent on the GPU";
		MESSAGE(report.str());

		CHECK(renderer.drawn.back() == skinnable.skinned_id);
		CHECK(renderer.handed == handed);
		CHECK(cpu / gpu >= LEAST_GPU_GAIN);
	};

	#pragma endregion
};

#include "doctest.h"

#include "Test Support.h"

#include "Camera.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"
#include "Sprite.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

using LoomTests::Pump;
using LoomTests::Probe;
using LoomTests::RecordingRenderer;

#pragma region Helpers

namespace
{
	bool Registered(const Loom::Scene* scene)
	{
		const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();

		return std::find(scenes.begin(), scenes.end(), scene) != scenes.end();
	};
};

#pragma endregion


TEST_SUITE("Scene")
{
	#pragma region Names and Threads

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene names itself and its root")
	{
		Loom::Scene scene("Level One");
		Pump();

		CHECK(scene.GetName() == "Level One");
		CHECK(scene.GetRoot().GetName() == "Root");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene defaults to the name Scene on thread 0")
	{
		Loom::Scene scene;
		Pump();

		CHECK(scene.GetName() == "Scene");
		CHECK(scene.GetThreadID() == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene remembers the thread it was built for")
	{
		Loom::Scene scene("Threaded", 3);
		Pump();

		CHECK(scene.GetThreadID() == 3);
	};

	#pragma endregion

	#pragma region Registry

	// Engine iterates GetScenes() every frame, so registration is what makes a
	// scene actually run. Like everything else, it happens on the task queue.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene registers itself once the queue drains")
	{
		Loom::Scene scene("Registered");

		CHECK_FALSE(Registered(&scene));

		Pump();

		CHECK(Registered(&scene));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a destroyed scene unregisters itself")
	{
		const Loom::Scene* address = nullptr;

		{
			Loom::Scene scene("Temporary");
			Pump();

			address = &scene;
			REQUIRE(Registered(address));
		};

		Pump();

		CHECK_FALSE(Registered(address));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "several scenes coexist in the registry")
	{
		const size_t before = Loom::Scene::GetScenes().size();

		Loom::Scene first("First");
		Loom::Scene second("Second");
		Pump();

		CHECK(Loom::Scene::GetScenes().size() == before + 2);
		CHECK(Registered(&first));
		CHECK(Registered(&second));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Unlist keeps a scene out of the registry for good")
	{
		Loom::Scene listed("Listed");
		Loom::Scene unlisted("Unlisted");
		Loom::Scene::Unlist(&unlisted);
		Pump();

		CHECK(Registered(&listed));
		CHECK_FALSE(Registered(&unlisted));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "MoveScene puts a scene at a position in the registry")
	{
		const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();
		const size_t before = scenes.size();

		Loom::Scene first("First");
		Loom::Scene second("Second");
		Pump();

		Loom::Scene::MoveScene(&second, before);
		Pump();

		CHECK(scenes[before] == &second);
		CHECK(scenes[before + 1] == &first);

		Loom::Scene::MoveScene(&second, scenes.size() + 1);
		Pump();

		CHECK(scenes.back() == &second);
	};

	#pragma endregion

	#pragma region Root

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetRoot hands out the same object every time")
	{
		Loom::Scene scene("Root identity");
		Pump();

		CHECK(&scene.GetRoot() == &scene.GetRoot());

		const Loom::Scene& asConst = scene;
		CHECK(&asConst.GetRoot() == &scene.GetRoot());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Attach on a scene attaches to its root")
	{
		Loom::Scene scene("Scene attach");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		CHECK(scene.GetRoot().GetComponent<Probe<>>() == probe);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "AddChild on a scene adds under its root")
	{
		Loom::Scene scene("Scene child");
		Pump();

		Loom::GameObject* child = scene.AddChild("Child");
		Pump();

		REQUIRE(scene.GetRoot().GetChildren().size() == 1);
		CHECK(scene.GetRoot().GetChildren()[0] == child);
	};

	#pragma endregion

	#pragma region Ticking

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Update and Render each drive only their own callback")
	{
		Loom::Scene scene("Scene ticks");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		scene.Update();
		CHECK(probe->Count("update") == 1);
		CHECK(probe->Count("render") == 0);

		scene.Render();
		CHECK(probe->Count("render") == 1);

		// Ticking one pass must not tick another.
		CHECK(probe->Count("update") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a pass gives each program its view once, a pass through another camera gives it again, and outside a pass it is given every time")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===VERTEX===\nuniform mat4 u_model;\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		Loom::Shader shader("viewed once a pass", source);

		Loom::Scene scene("viewed once a pass");
		Pump();

		scene.Attach<Loom::Camera>();

		const glm::vec3 away(0.0f, 0.0f, 10.0f);

		Loom::GameObject* elsewhere = scene.AddChild("Elsewhere");
		elsewhere->transform.position = away;
		Loom::Camera* other = elsewhere->Attach<Loom::Camera>();

		// Each drawn alone, so each readies the program itself.
		for (const char* name : { "First", "Second", "Third" })
		{
			Loom::GameObject* holder = scene.AddChild(name);
			holder->Attach<Loom::Material>()->shader = &shader;

			Loom::Mesh* mesh = holder->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
			mesh->m_vertices = std::vector<float>(9, 1.0f);
			mesh->m_draw_type = Loom::Mesh::Dynamic;
		};

		Pump();

		scene.Render();

		CHECK(renderer.drawn.size() == 3);
		CHECK(renderer.viewed == std::vector<uint32_t>{ shader.id });

		const glm::mat4 own = renderer.matrices["u_viewProjection"];

		scene.Render(other);

		CHECK(renderer.viewed == std::vector<uint32_t>{ shader.id, shader.id });
		CHECK(renderer.matrices["u_viewProjection"] != own);

		Loom::Scene::ApplyView(shader.id);
		Loom::Scene::ApplyView(shader.id);

		CHECK(renderer.viewed.size() == 4);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sprite in a scene with no camera is drawn in world space")
	{
		RecordingRenderer renderer;
		renderer.matrices["u_viewProjection"] = glm::mat4(0.0f);

		Loom::Scene scene("sprite without a camera");
		scene.Attach<Loom::Sprite>();
		Pump();

		scene.Render();

		CHECK(renderer.viewed.size() == 1);
		CHECK(renderer.matrices["u_viewProjection"] == glm::mat4(1.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Physics drives OnPhysics")
	{
		Loom::Scene scene("Scene physics");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		scene.Physics();

		CHECK(probe->Count("physics") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "each physics step runs OnFixedUpdate once, ahead of OnPhysics")
	{
		Loom::Scene scene("Scene fixed");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		scene.Physics();
		scene.Physics();

		CHECK(probe->Count("fixed") == 2);

		const auto fixed = std::find(probe->calls.begin(), probe->calls.end(), "fixed");
		const auto physics = std::find(probe->calls.begin(), probe->calls.end(), "physics");

		CHECK(fixed < physics);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a disabled object gets no OnFixedUpdate")
	{
		Loom::Scene scene("Scene fixed disabled");
		Pump();

		Loom::GameObject* parent = scene.AddChild("Parent");
		Loom::GameObject* child = parent->AddChild("Child");
		Pump();

		Probe<>* probe = child->Attach<Probe<>>();
		Pump();

		parent->SetDisabled(true);
		scene.Physics();

		CHECK(probe->Count("fixed") == 0);
	};

	struct FixedThrower final : Loom::Component<FixedThrower>
	{
		void OnFixedUpdate() override { throw std::runtime_error("fixed"); };
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a throw in OnFixedUpdate leaves the rest theirs")
	{
		Loom::Scene scene("Scene fixed throw");
		Pump();

		scene.AddChild("Thrower")->Attach<FixedThrower>();
		Probe<>* probe = scene.AddChild("Probe")->Attach<Probe<>>();
		Pump();

		scene.Physics();

		CHECK(probe->Count("fixed") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "ticking an empty scene does nothing")
	{
		Loom::Scene scene("Empty");
		Pump();

		scene.Update();
		scene.Render();
		scene.Physics();

		CHECK(scene.GetRoot().GetChildren().empty());
	};

	#pragma endregion

	#pragma region Identity

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene is a LoomObject with its own ID")
	{
		Loom::Scene first("A");
		Loom::Scene second("B");
		Pump();

		CHECK(first.m_ID != second.m_ID);
		CHECK(first.NameAndID() == "A (ID: " + std::to_string(first.m_ID) + ')');
	};

	// Engine flips this while the frame loop is running; scenes read it to know
	// whether they are being ticked by the engine or by a tool.
	TEST_CASE("a scene knows the engine is not running")
	{
		CHECK_FALSE(Loom::Scene::is_engine_running.load());
	};

	#pragma endregion
};

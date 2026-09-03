#include "doctest.h"

#include "Test Support.h"

#include "Scene.h"

#include <algorithm>
#include <string>

using LoomTests::Pump;
using LoomTests::Probe;

namespace
{
	bool Registered(const Loom::Scene* scene)
	{
		const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();

		return std::find(scenes.begin(), scenes.end(), scene) != scenes.end();
	};
};


TEST_SUITE("Scene")
{
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

	// See "Physics reaches the hierarchy too" in the GameObject suite:
	// GameObject::Attach never adds a component to m_physicsables, so the
	// physics pass has nothing to walk.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Physics drives OnPhysics" * doctest::should_fail())
	{
		Loom::Scene scene("Scene physics");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		scene.Physics();

		CHECK(probe->Count("physics") == 1);
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
};

#include "doctest.h"

#include "Test Support.h"

#include "Scene.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::Probe;

namespace
{
	bool Registered(const Loom::Scene* scene)
	{
		const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();

		return std::find(scenes.begin(), scenes.end(), scene) != scenes.end();
	};

	// The open scene called name, or null.
	Loom::Scene* Open(const std::string& name)
	{
		for (Loom::Scene* scene : Loom::Scene::GetScenes())
			if (scene->GetName() == name)
				return scene;

		return nullptr;
	};

	std::string TempScene(const std::string& name)
	{
		return (std::filesystem::temp_directory_path() / (name + Loom::SceneSerializer::extension)).string();
	};

	// A scene of that name with one child under its root, saved to the temp
	// folder for as long as this lives.
	struct SceneFile
	{
		explicit SceneFile(const std::string& name) :
			path(TempScene(name))
		{
			Loom::Scene scene(name);
			scene.AddChild("Child");
			Pump();

			Loom::SceneSerializer::SaveToFile(scene, path);
		};

		~SceneFile()
		{
			std::filesystem::remove(path);
		};

		const std::string path;
	};

	// Points at a GameObject, which a scene file writes after it when that is
	// a child of the one it is on.
	struct ForwardReference final : Loom::Component<ForwardReference>
	{
		Loom::Serial<Loom::GameObject*> target;
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Physics drives OnPhysics")
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

	// A script asks from inside an update, which is walking the scenes Load
	// is about to delete.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Load swaps every open scene for the file's once the frame is over")
	{
		const SceneFile file("Level");

		new Loom::Scene("Menu");
		new Loom::Scene("Overlay");
		Pump();

		Loom::Scene::Load(file.path);
		Pump();

		CHECK(Open("Menu") != nullptr);
		CHECK(Open("Overlay") != nullptr);

		Loom::Engine::DoRequests();
		Pump();

		REQUIRE(Loom::Scene::GetScenes().size() == 1);

		Loom::Scene* level = Open("Level");
		REQUIRE(level != nullptr);
		CHECK(level->GetRoot().GetChildren().size() == 1);

		delete level;
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the last scene asked for before the frame is over is the one loaded")
	{
		const SceneFile first("First");
		const SceneFile last("Last");
		Pump();

		Loom::Scene::Load(first.path);
		Loom::Scene::Load(last.path);
		Loom::Engine::DoRequests();
		Pump();

		CHECK(Open("First") == nullptr);

		Loom::Scene* loaded = Open("Last");
		CHECK(loaded != nullptr);

		delete loaded;
	};

	// Restarting a level loads the file its scene came from while that scene
	// is open, and a reference read while the old objects still answered to
	// their guids would point into them.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene loaded over itself keeps its references")
	{
		const std::string path = TempScene("Restarted");

		Loom::Scene* scene = new Loom::Scene("Restarted");
		Loom::GameObject* child = scene->AddChild("Child");
		ForwardReference* reference = scene->Attach<ForwardReference>();
		Pump();

		reference->target = child;
		Loom::SceneSerializer::SaveToFile(*scene, path);

		Loom::Scene::Load(path);
		Loom::Engine::DoRequests();
		Pump();

		Loom::Scene* restarted = Open("Restarted");
		REQUIRE(restarted != nullptr);
		REQUIRE(restarted->GetRoot().GetChildren().size() == 1);

		const ForwardReference* loaded = restarted->GetRoot().GetComponent<ForwardReference>();
		REQUIRE(loaded != nullptr);
		CHECK(loaded->target == restarted->GetRoot().GetChildren().front());

		delete restarted;
		std::filesystem::remove(path);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene that is not there is reported and changes nothing")
	{
		new Loom::Scene("Kept");
		Pump();

		std::ostringstream captured;
		std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());

		Loom::Scene::Load(TempScene("Missing"));
		Loom::Engine::DoRequests();

		std::cerr.rdbuf(previous);
		Pump();

		CHECK(captured.str().find("Missing") != std::string::npos);

		Loom::Scene* kept = Open("Kept");
		CHECK(kept != nullptr);

		delete kept;
	};

	// The editor plays a scene it owns, and puts the scenes Play began with
	// back itself.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a load function carries out Load in place of the swap")
	{
		const SceneFile file("Handed Over");

		new Loom::Scene("Owned");
		Pump();

		std::vector<std::string> asked;

		Loom::Scene::SetLoadFunction([&asked](const std::string& path) { asked.push_back(path); });
		Loom::Scene::Load(file.path);
		Loom::Engine::DoRequests();
		Loom::Scene::SetLoadFunction(nullptr);
		Pump();

		CHECK(asked == std::vector<std::string>{ file.path });
		CHECK(Open("Handed Over") == nullptr);

		Loom::Scene* owned = Open("Owned");
		CHECK(owned != nullptr);

		delete owned;
	};
};

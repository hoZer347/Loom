#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneHistory.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <string>
#include <vector>

using LoomTests::Pump;

namespace
{
	// A component holding a reference, so undo can be checked for putting
	// links between objects back as well as the objects themselves.
	struct HistoryMarker final : Loom::Component<HistoryMarker>
	{
		LOOM_SERIAL(std::string, label, "unset");
		LOOM_SERIAL(Loom::GameObject*, partner);
	};

	struct HistoryMarkerRegistered
	{
		HistoryMarkerRegistered()
		{
			Loom::ComponentRegistry::Register<HistoryMarker>("HistoryMarker");
		};

		~HistoryMarkerRegistered()
		{
			Loom::ComponentRegistry::Unregister("HistoryMarker");
		};
	};

	// Plays the editor's part: owns the open scenes, and swaps one for its
	// rebuilt self when the history asks.
	struct Editing : LoomTests::EngineFixture
	{
		~Editing()
		{
			for (Loom::Scene* scene : open)
				delete scene;

			Pump();
		};

		Loom::Scene* Open(const std::string& name)
		{
			Loom::Scene* scene = new Loom::Scene(name);
			Pump();

			open.push_back(scene);

			return scene;
		};

		void Close(Loom::Scene* scene)
		{
			std::erase(open, scene);
			history.Forget(scene->GetGuid());

			delete scene;
			Pump();
		};

		Loom::SceneHistory::Rebuild Rebuild()
		{
			return
				[this](Loom::Scene* scene, const std::string& text)
				{
					Loom::Scene* replacement = Loom::SceneHistory::Replace(scene, text);

					const auto slot = std::find(open.begin(), open.end(), scene);

					if (replacement)
						*slot = replacement;
					else open.erase(slot);

					return replacement;
				};
		};

		void Record() { history.Record(open); };
		bool Undo() { return history.Undo(open, Rebuild()); };
		bool Redo() { return history.Redo(open, Rebuild()); };

		std::string Text(size_t index)
		{
			Pump();
			return Loom::SceneSerializer::Serialize(*open[index]);
		};

		Loom::GameObject& Root(size_t index)
		{
			return open[index]->GetRoot();
		};

		// Each edit renames the root's only child, so every stage of a run of
		// edits serializes differently.
		void Rename(size_t index, const std::string& name)
		{
			Root(index).GetChildren().front()->SetName(name);
		};

		std::vector<Loom::Scene*> open{ };
		Loom::SceneHistory history{ };
	};
};


TEST_SUITE("SceneHistory")
{
	TEST_CASE_FIXTURE(Editing, "a scene seen for the first time is remembered, not recorded")
	{
		Open("First");

		Record();

		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK_FALSE(Undo());
	};

	TEST_CASE_FIXTURE(Editing, "a scene that has not changed records nothing")
	{
		Open("Still");

		Record();
		Record();
		Record();

		CHECK_FALSE(history.CanUndo());
	};

	TEST_CASE_FIXTURE(Editing, "an edit is one step, and undo and redo put the text back exactly")
	{
		Open("Edited");
		Record();

		const std::string before = Text(0);

		Root(0).AddChild("Added");
		Record();

		const std::string after = Text(0);

		REQUIRE(history.CanUndo());
		REQUIRE(before != after);

		CHECK(Undo());
		CHECK(Text(0) == before);
		CHECK(Root(0).GetChildren().empty());
		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());

		// Comparing again after an undo finds the rebuilt scene where the undo
		// left it: no new step, and redo is still there.
		Record();

		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());

		CHECK(Redo());
		CHECK(Text(0) == after);
		CHECK(Root(0).GetChildren().size() == 1);
		CHECK(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
	};

	TEST_CASE_FIXTURE(Editing, "every stage of a run of edits comes back exactly, both ways")
	{
		constexpr int edits = 4;

		Open("Run");
		Root(0).AddChild("Stage 0");
		Record();

		std::vector<std::string> stages{ Text(0) };

		for (int i = 1; i <= edits; i++)
		{
			Rename(0, "Stage " + std::to_string(i));
			Record();
			stages.push_back(Text(0));
		};

		for (int i = edits - 1; i >= 0; i--)
		{
			REQUIRE(Undo());
			CHECK(Text(0) == stages[i]);
		};

		CHECK_FALSE(Undo());
		CHECK(Text(0) == stages.front());

		for (int i = 1; i <= edits; i++)
		{
			REQUIRE(Redo());
			CHECK(Text(0) == stages[i]);
		};

		CHECK_FALSE(Redo());
	};

	TEST_CASE_FIXTURE(Editing, "an edit made after an undo throws redo away")
	{
		Open("Branch");
		Root(0).AddChild("Original");
		Record();

		Rename(0, "First");
		Record();

		REQUIRE(Undo());
		REQUIRE(history.CanRedo());

		Rename(0, "Second");
		Record();

		CHECK_FALSE(history.CanRedo());
		CHECK_FALSE(Redo());
		CHECK(Root(0).GetChildren().front()->GetName() == "Second");
	};

	TEST_CASE_FIXTURE(Editing, "an edit not compared yet is the one undo takes back")
	{
		Open("Unrecorded");
		Root(0).AddChild("Before");
		Record();

		const std::string before = Text(0);

		Rename(0, "After");

		const std::string after = Text(0);

		CHECK(Undo());
		CHECK(Text(0) == before);

		CHECK(Redo());
		CHECK(Text(0) == after);
	};

	TEST_CASE_FIXTURE(Editing, "undo works back through several scenes, newest edit first")
	{
		Open("A");
		Open("B");
		Root(0).AddChild("A0");
		Root(1).AddChild("B0");
		Record();

		Rename(0, "A1");
		Record();

		Rename(1, "B1");
		Record();

		Rename(0, "A2");
		Record();

		REQUIRE(Undo());
		CHECK(Root(0).GetChildren().front()->GetName() == "A1");
		CHECK(Root(1).GetChildren().front()->GetName() == "B1");

		REQUIRE(Undo());
		CHECK(Root(0).GetChildren().front()->GetName() == "A1");
		CHECK(Root(1).GetChildren().front()->GetName() == "B0");

		REQUIRE(Undo());
		CHECK(Root(0).GetChildren().front()->GetName() == "A0");
		CHECK(Root(1).GetChildren().front()->GetName() == "B0");

		CHECK_FALSE(Undo());
	};

	TEST_CASE_FIXTURE(Editing, "a step for a scene that has closed is skipped and thrown away")
	{
		Open("Kept");
		Loom::Scene* closing = Open("Closing");
		Root(0).AddChild("Kept 0");
		Root(1).AddChild("Closing 0");
		Record();

		Rename(0, "Kept 1");
		Record();

		Rename(1, "Closing 1");
		Record();

		Close(closing);

		REQUIRE(Undo());
		CHECK(Root(0).GetChildren().front()->GetName() == "Kept 0");

		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());

		REQUIRE(Redo());
		CHECK_FALSE(history.CanRedo());
	};

	TEST_CASE_FIXTURE(Editing, "only the newest max_steps edits are kept")
	{
		constexpr size_t extra = 5;
		constexpr size_t edits = Loom::SceneHistory::max_steps + extra;

		Open("Long");
		Root(0).AddChild("Edit 0");
		Record();

		for (size_t i = 1; i <= edits; i++)
		{
			Rename(0, "Edit " + std::to_string(i));
			Record();
		};

		size_t undone = 0;

		while (Undo())
			undone++;

		CHECK(undone == Loom::SceneHistory::max_steps);
		CHECK(Root(0).GetChildren().front()->GetName() == "Edit " + std::to_string(extra));
	};

	TEST_CASE_FIXTURE(Editing, "a forgotten scene starts again from where it is")
	{
		Open("Forgotten");
		Record();

		Root(0).AddChild("Unseen");
		history.Forget(open[0]->GetGuid());
		Record();

		CHECK_FALSE(history.CanUndo());
	};

	TEST_CASE_FIXTURE(Editing, "a scene rebuilt under the same guid keeps its history")
	{
		Open("Rebuilt");
		Root(0).AddChild("Before");
		Record();

		Rename(0, "After");
		Record();

		// What a script compile or a Stop does: the same scene, a new object.
		open[0] = Loom::SceneHistory::Replace(open[0], Text(0));
		REQUIRE(open[0] != nullptr);

		Record();

		REQUIRE(Undo());
		CHECK(Root(0).GetChildren().front()->GetName() == "Before");
		CHECK_FALSE(history.CanUndo());
	};

	TEST_CASE_FIXTURE(Editing, "undo brings objects back under their own guids, references and all")
	{
		HistoryMarkerRegistered registered;

		Open("Identity");
		Loom::GameObject* holder = Root(0).AddChild("Holder");
		Loom::GameObject* target = Root(0).AddChild("Target");
		Pump();

		HistoryMarker* marker = holder->Attach<HistoryMarker>();
		Pump();

		marker->label = "kept";
		marker->partner = target;

		const Loom::Guid holder_guid = holder->GetGuid();
		const Loom::Guid target_guid = target->GetGuid();
		const Loom::Guid marker_guid = marker->GetGuid();

		Record();

		target->Destroy();
		Record();

		REQUIRE(Root(0).GetChildren().size() == 1);

		REQUIRE(Undo());

		Loom::GameObject* restored_holder = Loom::LoomObject::GetByGuid<Loom::GameObject>(holder_guid);
		Loom::GameObject* restored_target = Loom::LoomObject::GetByGuid<Loom::GameObject>(target_guid);

		REQUIRE(restored_holder != nullptr);
		REQUIRE(restored_target != nullptr);
		CHECK(restored_target->GetName() == "Target");

		HistoryMarker* restored_marker = restored_holder->GetComponent<HistoryMarker>();

		REQUIRE(restored_marker != nullptr);
		CHECK(restored_marker->GetGuid() == marker_guid);
		CHECK(*restored_marker->label == "kept");
		CHECK(restored_marker->partner == restored_target);
	};

	TEST_CASE_FIXTURE(Editing, "undo leaves the scene where it was among the open scenes")
	{
		Open("First");
		Open("Middle");
		Open("Last");
		Root(1).AddChild("Before");
		Record();

		const auto position =
			[](Loom::Scene* scene)
			{
				const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();
				return std::find(scenes.begin(), scenes.end(), scene) - scenes.begin();
			};

		const auto before = position(open[1]);

		Rename(1, "After");
		Record();

		REQUIRE(Undo());
		CHECK(position(open[1]) == before);
	};

	TEST_CASE_FIXTURE(Editing, "Replace refuses text that is not a scene")
	{
		Loom::Scene* scene = Open("Doomed");
		std::erase(open, scene);

		std::string error;

		CHECK(Loom::SceneHistory::Replace(scene, "not a scene", &error) == nullptr);
		CHECK_FALSE(error.empty());

		const std::vector<Loom::Scene*>& scenes = Loom::Scene::GetScenes();
		CHECK(std::find(scenes.begin(), scenes.end(), scene) == scenes.end());
	};

	TEST_CASE_FIXTURE(Editing, "a rebuild that fails uses the step up and lets the scene go")
	{
		Open("Failing");
		Root(0).AddChild("Before");
		Record();

		Rename(0, "After");
		Record();

		const Loom::SceneHistory::Rebuild failing =
			[this](Loom::Scene* scene, const std::string&) -> Loom::Scene*
			{
				std::erase(open, scene);
				delete scene;
				Pump();

				return nullptr;
			};

		CHECK_FALSE(history.Undo(open, failing));
		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK(open.empty());
	};

	TEST_CASE_FIXTURE(Editing, "undo and redo with nothing to step leave the scene alone")
	{
		Loom::Scene* scene = Open("Untouched");
		Record();

		CHECK_FALSE(Undo());
		CHECK_FALSE(Redo());
		CHECK(open.front() == scene);
	};
};

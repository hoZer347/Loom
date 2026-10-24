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
		Loom::Serial<std::string> label = "unset";
		Loom::Serial<Loom::GameObject*> partner;
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

	// Plays the editor's part: owns the open scenes, and watches the journal
	// the history reads.
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

		// The same scene opened again from what the first copy saves.
		Loom::Scene* Copy(size_t index)
		{
			Loom::Scene* scene = Loom::SceneSerializer::Deserialize(Text(index));
			Pump();

			open.push_back(scene);

			return scene;
		};

		void Close(Loom::Scene* scene)
		{
			std::erase(open, scene);

			delete scene;
			Pump();
		};

		void Record() { history.Record(open); };
		bool Undo() { return history.Undo(open); };
		bool Redo() { return history.Redo(open); };

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

		LoomTests::Watching watching{ };
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

	// The inspector's remove button on a script component whose library has
	// not built, taken back.
	TEST_CASE_FIXTURE(Editing, "undo puts back a component of a type not registered, as it was")
	{
		std::string text;

		{
			HistoryMarkerRegistered registered;

			Loom::Scene scene("Missing");
			Loom::GameObject* holder = scene.AddChild("Holder");
			Pump();

			holder->Attach<HistoryMarker>()->label = "kept";
			Pump();

			text = Loom::SceneSerializer::Serialize(scene);
		};

		Pump();

		open.push_back(Loom::SceneSerializer::Deserialize(text));
		Pump();
		Record();

		Loom::GameObject* holder = Root(0).GetChildren().front();

		REQUIRE(holder->GetComponents().size() == 1);

		holder->DetachComponent(holder->GetComponents().front());
		Pump();
		Record();

		REQUIRE(Undo());
		CHECK(Text(0) == text);
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

	TEST_CASE_FIXTURE(Editing, "undo and redo with nothing to step leave the scene alone")
	{
		Loom::Scene* scene = Open("Untouched");
		Record();

		CHECK_FALSE(Undo());
		CHECK_FALSE(Redo());
		CHECK(open.front() == scene);
	};

	TEST_CASE_FIXTURE(Editing, "an object moved to another scene goes back in one undo, and over again in one redo")
	{
		Open("From");
		Open("To");
		Loom::GameObject* moved = Root(0).AddChild("Moved");
		Root(1).AddChild("Stays");
		Record();

		const std::string from = Text(0);
		const std::string to = Text(1);
		const Loom::Guid guid = moved->GetGuid();

		moved->SetParent(&Root(1));
		Record();

		const std::string from_after = Text(0);
		const std::string to_after = Text(1);

		REQUIRE(Undo());
		CHECK(Text(0) == from);
		CHECK(Text(1) == to);
		CHECK(Loom::LoomObject::GetByGuid<Loom::GameObject>(guid)->GetParent() == &Root(0));
		CHECK_FALSE(history.CanUndo());

		REQUIRE(Redo());
		CHECK(Text(0) == from_after);
		CHECK(Text(1) == to_after);
		CHECK(Loom::LoomObject::GetByGuid<Loom::GameObject>(guid)->GetParent() == &Root(1));
		CHECK_FALSE(history.CanRedo());
	};

	TEST_CASE_FIXTURE(Editing, "undo puts a step back in place, leaving the rest of the scene as it was")
	{
		Open("InPlace");
		Loom::GameObject* kept = Root(0).AddChild("Kept");
		Loom::GameObject* edited = Root(0).AddChild("Edited");
		Pump();
		Record();

		Loom::Scene* scene = open[0];
		const std::string before = Text(0);

		edited->SetName("Renamed");
		Record();

		REQUIRE(Undo());
		CHECK(open[0] == scene);
		CHECK(Root(0).GetChildren().front() == kept);
		CHECK(Root(0).GetChildren().back() == edited);
		CHECK(Text(0) == before);
	};

	TEST_CASE_FIXTURE(Editing, "a scene open twice is one scene: an edit to either copy is made to both, and undone in both")
	{
		Open("Twice");
		Root(0).AddChild("Original");
		Pump();
		Record();

		Copy(0);
		Record();

		const std::string before = Text(0);
		REQUIRE(Text(1) == before);

		Rename(1, "Changed");
		Record();

		CHECK(Root(0).GetChildren().front()->GetName() == "Changed");
		CHECK(Text(0) == Text(1));

		REQUIRE(Undo());
		CHECK(Text(0) == before);
		CHECK(Text(1) == before);
	};

	TEST_CASE_FIXTURE(Editing, "a scene opened again after an edit is made the same as the copy already open")
	{
		Open("Stale");
		Root(0).AddChild("Original");
		Pump();
		Record();

		const std::string saved = Text(0);

		Rename(0, "Edited");
		Record();

		Loom::Scene* stale = Loom::SceneSerializer::Deserialize(saved);
		Pump();
		open.push_back(stale);
		Record();

		CHECK(Text(1) == Text(0));
		CHECK(Root(1).GetChildren().front()->GetName() == "Edited");
	};

	TEST_CASE_FIXTURE(Editing, "Rewrite makes an object what the text says, in place, as one undoable edit")
	{
		Open("Rewritten");
		Loom::GameObject* holder = Root(0).AddChild("Holder");
		Pump();
		Loom::GameObject* inner = holder->AddChild("Inner");
		Pump();

		const std::string wanted = Loom::SceneSerializer::Serialize(*holder);

		inner->SetName("Renamed");
		holder->AddChild("Extra");
		Pump();
		Record();

		const std::string edited = Text(0);

		history.Rewrite(*holder, wanted);
		Record();

		CHECK(Loom::SceneSerializer::Serialize(*holder) == wanted);
		REQUIRE(holder->GetChildren().size() == 1);
		CHECK(holder->GetChildren().front() == inner);

		REQUIRE(Undo());
		CHECK(Text(0) == edited);
		CHECK_FALSE(history.CanUndo());
	};
};

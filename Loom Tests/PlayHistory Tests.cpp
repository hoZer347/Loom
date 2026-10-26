#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "PlayHistory.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using LoomTests::Pump;

namespace
{
	// A component with a field to edit and a reference to put back.
	struct PlayMarker final : Loom::Component<PlayMarker>
	{
		Loom::Serial<std::string> label = "unset";
		Loom::Serial<PlayMarker*> partner;
	};

	// A scene with one child to edit, as the Inspector would while playing.
	struct Running : LoomTests::EngineFixture
	{
		Running()
		{
			Loom::ComponentRegistry::Register<PlayMarker>("PlayMarker");
			Pump();
			cube = scene->GetRoot().AddChild("Cube");
			Pump();
		};

		~Running()
		{
			scene.reset();
			Pump();
			Loom::ComponentRegistry::Unregister("PlayMarker");
		};

		// Sets serial as a drag would, and records it as letting go does.
		template <typename T, typename U>
		void Edit(Loom::LoomObject& object, Loom::Serial<T>& serial, const U& value)
		{
			const std::vector<Loom::SerializedField>& fields = object.GetFields();
			const T& held = serial;
			const size_t index = std::find_if(
				fields.begin(),
				fields.end(),
				[&held](const Loom::SerializedField& field) { return field.data == &held; }) - fields.begin();

			REQUIRE(index < fields.size());

			const std::string before = fields[index].Write();

			serial = value;
			history.RecordField(object, index, before);
		};

		// Changes cube's components as the Inspector does, and records it once
		// the queue has caught up.
		void Change(const std::function<void()>& change)
		{
			const std::string before = Loom::SceneSerializer::SerializeOwn(*cube);

			change();
			Pump();
			history.RecordComponents(*cube, before);
		};

		PlayMarker* Marker(const std::string& label)
		{
			PlayMarker* marker = cube->Attach<PlayMarker>();

			marker->label = label;
			Pump();

			return marker;
		};

		std::vector<std::string> Labels()
		{
			std::vector<std::string> labels{ };

			for (Loom::ComponentBase* component : cube->GetComponents())
				if (PlayMarker* marker = dynamic_cast<PlayMarker*>(component))
					labels.push_back(*marker->label);

			return labels;
		};

		std::unique_ptr<Loom::Scene> scene = std::make_unique<Loom::Scene>("Running");
		Loom::GameObject* cube = nullptr;
		Loom::PlayHistory history{ };
	};
};


TEST_SUITE("PlayHistory")
{
	TEST_CASE_FIXTURE(Running, "an edit is one step, and undo and redo put its value back and forward")
	{
		const glm::vec3 moved(1.0f, 2.0f, 3.0f);

		Edit(*cube, cube->transform.position, moved);

		REQUIRE(history.CanUndo());
		CHECK(history.Undo() == cube);
		CHECK(*cube->transform.position == glm::vec3(0.0f));
		CHECK_FALSE(history.CanUndo());

		REQUIRE(history.CanRedo());
		CHECK(history.Redo() == cube);
		CHECK(*cube->transform.position == moved);
		CHECK_FALSE(history.CanRedo());
	};

	TEST_CASE_FIXTURE(Running, "an edit that changed nothing is not a step")
	{
		Edit(*cube, cube->transform.scale, glm::vec3(1.0f));

		CHECK_FALSE(history.CanUndo());
		CHECK(history.Undo() == nullptr);
	};

	TEST_CASE_FIXTURE(Running, "steps go back newest first, each to its own field")
	{
		const glm::vec3 turned(0.0f, 90.0f, 0.0f);
		const glm::vec3 grown(2.0f);

		Edit(*cube, cube->transform.rotation, turned);
		Edit(*cube, cube->transform.scale, grown);

		history.Undo();

		CHECK(*cube->transform.scale == glm::vec3(1.0f));
		CHECK(*cube->transform.rotation == turned);

		history.Undo();

		CHECK(*cube->transform.rotation == glm::vec3(0.0f));
	};

	TEST_CASE_FIXTURE(Running, "a step writes only its field, leaving what the run did to the others")
	{
		const glm::vec3 walked(0.0f, 0.0f, 5.0f);

		Edit(*cube, cube->transform.scale, glm::vec3(3.0f));
		cube->transform.position = walked;

		history.Undo();

		CHECK(*cube->transform.scale == glm::vec3(1.0f));
		CHECK(*cube->transform.position == walked);
	};

	TEST_CASE_FIXTURE(Running, "a component's field goes back, and the step is on its GameObject")
	{
		PlayMarker* marker = Marker("first");

		Edit(*marker, marker->label, std::string("second"));

		CHECK(history.Undo() == cube);
		CHECK(*marker->label == "first");

		CHECK(history.Redo() == cube);
		CHECK(*marker->label == "second");
	};

	TEST_CASE_FIXTURE(Running, "a new edit drops what was undone")
	{
		Edit(*cube, cube->transform.position, glm::vec3(1.0f));
		history.Undo();

		Edit(*cube, cube->transform.scale, glm::vec3(2.0f));

		CHECK_FALSE(history.CanRedo());
		CHECK(history.Redo() == nullptr);
	};

	TEST_CASE_FIXTURE(Running, "a step whose object has gone is passed over for the one before it")
	{
		Loom::GameObject* other = scene->GetRoot().AddChild("Other");
		Pump();

		Edit(*cube, cube->transform.position, glm::vec3(1.0f));
		Edit(*other, other->transform.position, glm::vec3(2.0f));

		other->Destroy();
		Pump();

		CHECK(history.Undo() == cube);
		CHECK(*cube->transform.position == glm::vec3(0.0f));
		CHECK_FALSE(history.CanUndo());
	};

	TEST_CASE_FIXTURE(Running, "only the newest steps are kept")
	{
		for (size_t step = 1; step <= Loom::PlayHistory::max_steps + 1; step++)
			Edit(*cube, cube->transform.position, glm::vec3((float)step));

		size_t undone = 0;

		while (history.Undo())
			undone++;

		CHECK(undone == Loom::PlayHistory::max_steps);
		CHECK(*cube->transform.position == glm::vec3(1.0f));
	};

	TEST_CASE_FIXTURE(Running, "Clear forgets every step")
	{
		Edit(*cube, cube->transform.position, glm::vec3(1.0f));
		Edit(*cube, cube->transform.position, glm::vec3(2.0f));
		history.Undo();

		history.Clear();

		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
	};

	TEST_CASE_FIXTURE(Running, "undo puts a removed component back in its place, and redo takes it off again")
	{
		Marker("first");
		PlayMarker* second = Marker("second");
		Marker("third");

		const Loom::Guid guid = second->GetGuid();

		Change([&]() { cube->DetachComponent(second); });

		REQUIRE(Labels() == std::vector<std::string>{ "first", "third" });

		CHECK(history.Undo() == cube);
		Pump();

		CHECK(Labels() == std::vector<std::string>{ "first", "second", "third" });
		CHECK(Loom::LoomObject::GetByGuid<PlayMarker>(guid) == cube->GetComponents()[1]);

		history.Redo();
		Pump();

		CHECK(Labels() == std::vector<std::string>{ "first", "third" });
	};

	TEST_CASE_FIXTURE(Running, "undoing a removal puts back the references to it")
	{
		PlayMarker* holder = Marker("holder");
		PlayMarker* target = Marker("target");

		holder->partner = target;

		Change([&]() { cube->DetachComponent(target); });

		REQUIRE(holder->partner == nullptr);

		history.Undo();
		Pump();

		REQUIRE(cube->GetComponents().size() == 2);
		CHECK(holder->partner == cube->GetComponents()[1]);
	};

	TEST_CASE_FIXTURE(Running, "undo takes an added component off, and redo puts the same one back")
	{
		PlayMarker* added = nullptr;

		Change([&]() { added = Marker("added"); });

		const Loom::Guid guid = added->GetGuid();

		history.Undo();
		Pump();

		CHECK(cube->GetComponents().empty());

		history.Redo();
		Pump();

		REQUIRE(cube->GetComponents().size() == 1);
		CHECK(cube->GetComponents()[0]->GetGuid() == guid);
		CHECK(Labels() == std::vector<std::string>{ "added" });
	};

	TEST_CASE_FIXTURE(Running, "undo puts components back in the order they were in")
	{
		PlayMarker* first = Marker("first");
		PlayMarker* second = Marker("second");

		Change([&]() { cube->OrderComponents({ second, first }); });

		REQUIRE(Labels() == std::vector<std::string>{ "second", "first" });

		history.Undo();

		CHECK(Labels() == std::vector<std::string>{ "first", "second" });

		history.Redo();

		CHECK(Labels() == std::vector<std::string>{ "second", "first" });
	};

	TEST_CASE_FIXTURE(Running, "a component step leaves what the run has done since")
	{
		PlayMarker* removed = Marker("removed");
		const glm::vec3 walked(0.0f, 0.0f, 5.0f);

		Change([&]() { cube->DetachComponent(removed); });

		cube->transform.position = walked;
		Marker("spawned");

		history.Undo();
		Pump();

		CHECK(*cube->transform.position == walked);
		CHECK(Labels() == std::vector<std::string>{ "removed", "spawned" });
	};

	TEST_CASE_FIXTURE(Running, "a component the run has taken off since stays off when its edit is undone")
	{
		PlayMarker* kept = Marker("kept");
		PlayMarker* edited = Marker("edited");

		Change([&]() { cube->OrderComponents({ edited, kept }); });

		cube->DetachComponent(kept);
		Pump();

		history.Undo();
		Pump();

		CHECK(Labels() == std::vector<std::string>{ "edited" });
	};
};

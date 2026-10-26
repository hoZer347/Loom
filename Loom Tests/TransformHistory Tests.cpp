#include "doctest.h"

#include "Test Support.h"

#include "GameObject.h"
#include "Scene.h"
#include "TransformHistory.h"

#include "glm/glm.hpp"

#include <memory>

using LoomTests::Pump;
using Loom::Transform;

namespace
{
	// A scene with one child to edit, as the Inspector would while playing.
	struct Running : LoomTests::EngineFixture
	{
		Running()
		{
			Pump();
			cube = scene->GetRoot().AddChild("Cube");
			Pump();
		};

		// Sets field as a drag would, and records it as letting go does.
		void Edit(Loom::GameObject& gameObject, Loom::TransformHistory::Field field, const glm::vec3& value)
		{
			const glm::vec3 before = gameObject.transform.*field;

			gameObject.transform.*field = value;
			history.Record(gameObject, field, before);
		};

		std::unique_ptr<Loom::Scene> scene = std::make_unique<Loom::Scene>("Running");
		Loom::GameObject* cube = nullptr;
		Loom::TransformHistory history{ };
	};
};


TEST_SUITE("TransformHistory")
{
	TEST_CASE_FIXTURE(Running, "an edit is one step, and undo and redo put its value back and forward")
	{
		const glm::vec3 moved(1.0f, 2.0f, 3.0f);

		Edit(*cube, &Transform::position, moved);

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
		Edit(*cube, &Transform::scale, glm::vec3(1.0f));

		CHECK_FALSE(history.CanUndo());
		CHECK(history.Undo() == nullptr);
	};

	TEST_CASE_FIXTURE(Running, "steps go back newest first, each to its own field")
	{
		const glm::vec3 turned(0.0f, 90.0f, 0.0f);
		const glm::vec3 grown(2.0f);

		Edit(*cube, &Transform::rotation, turned);
		Edit(*cube, &Transform::scale, grown);

		history.Undo();

		CHECK(*cube->transform.scale == glm::vec3(1.0f));
		CHECK(*cube->transform.rotation == turned);

		history.Undo();

		CHECK(*cube->transform.rotation == glm::vec3(0.0f));
	};

	TEST_CASE_FIXTURE(Running, "a step writes only its field, leaving what the run did to the others")
	{
		const glm::vec3 walked(0.0f, 0.0f, 5.0f);

		Edit(*cube, &Transform::scale, glm::vec3(3.0f));
		cube->transform.position = walked;

		history.Undo();

		CHECK(*cube->transform.scale == glm::vec3(1.0f));
		CHECK(*cube->transform.position == walked);
	};

	TEST_CASE_FIXTURE(Running, "a new edit drops what was undone")
	{
		Edit(*cube, &Transform::position, glm::vec3(1.0f));
		history.Undo();

		Edit(*cube, &Transform::scale, glm::vec3(2.0f));

		CHECK_FALSE(history.CanRedo());
		CHECK(history.Redo() == nullptr);
	};

	TEST_CASE_FIXTURE(Running, "a step whose object has gone is passed over for the one before it")
	{
		Loom::GameObject* other = scene->GetRoot().AddChild("Other");
		Pump();

		Edit(*cube, &Transform::position, glm::vec3(1.0f));
		Edit(*other, &Transform::position, glm::vec3(2.0f));

		other->Destroy();
		Pump();

		CHECK(history.Undo() == cube);
		CHECK(*cube->transform.position == glm::vec3(0.0f));
		CHECK_FALSE(history.CanUndo());
	};

	TEST_CASE_FIXTURE(Running, "only the newest steps are kept")
	{
		for (size_t step = 1; step <= Loom::TransformHistory::max_steps + 1; step++)
			Edit(*cube, &Transform::position, glm::vec3((float)step));

		size_t undone = 0;

		while (history.Undo())
			undone++;

		CHECK(undone == Loom::TransformHistory::max_steps);
		CHECK(*cube->transform.position == glm::vec3(1.0f));
	};

	TEST_CASE_FIXTURE(Running, "Clear forgets every step")
	{
		Edit(*cube, &Transform::position, glm::vec3(1.0f));
		Edit(*cube, &Transform::position, glm::vec3(2.0f));
		history.Undo();

		history.Clear();

		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
	};
};

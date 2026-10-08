#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SerializedField.h"

#include "Utilities/StateMachine.h"

#include <memory>
#include <string>

using LoomTests::Pump;

namespace
{
	struct StateTestIdle final : Loom::State<StateTestIdle>
	{
	};

	// A machine is written the way a component is, and registers the same way.
	struct StateTestMachine final : Loom::StateMachine<StateTestMachine>
	{
	};

	struct StateTestWalk final : Loom::State<StateTestWalk, StateTestMachine>
	{
		void OnExit(Loom::StateBase* nextState) override { exits++; };

		static inline int exits = 0;
	};

	// Built by the machine that runs it, so not one to offer.
	struct StateTestInternal final : Loom::StateOf<StateTestMachine>
	{
	};

	// A machine on an object in a scene of its own.
	struct Machine
	{
		Machine()
		{
			object = scene.AddChild("Patrol");
			Pump();

			machine = object->Attach<StateTestMachine>();
			Pump();
		};

		const Loom::SerializedField& Field(size_t index) const
		{
			return machine->GetFields()[index];
		};

		Loom::Scene scene{ "Machine" };
		Loom::GameObject* object = nullptr;
		StateTestMachine* machine = nullptr;
	};

	constexpr size_t startField = 0;
	constexpr size_t currentField = 1;
};


TEST_SUITE("StateMachine")
{
	TEST_CASE("a state machine is offered under Add Component by its own name")
	{
		CHECK(Loom::ComponentRegistry::All().contains("StateTestMachine"));
	};

	TEST_CASE("a plain StateMachine is offered under Add Component")
	{
		CHECK(Loom::ComponentRegistry::All().contains("StateMachine"));
	};

	TEST_CASE("a State registers itself under its type name, and a StateOf does not")
	{
		CHECK(Loom::StateRegistry::Contains("StateTestIdle"));
		CHECK(Loom::StateRegistry::Contains("StateTestWalk"));
		CHECK_FALSE(Loom::StateRegistry::Contains("StateTestInternal"));

		CHECK(dynamic_cast<StateTestIdle*>(Loom::StateRegistry::Create("StateTestIdle").get()) != nullptr);
	};

	TEST_CASE("a reference built from a type is named the way its registration is")
	{
		const Loom::StateReference reference = Loom::StateReference::Of<StateTestIdle>();

		CHECK(reference.StateType() == "StateTestIdle");
		CHECK(Loom::StateReference::Named(reference.StateType()).IsSet());
	};

	TEST_CASE("a name nothing is registered under is kept, and builds nothing")
	{
		const Loom::StateReference reference = Loom::StateReference::Named("StateTestMissing");

		CHECK(reference.StateType() == "StateTestMissing");
		CHECK_FALSE(reference.IsSet());
		CHECK(reference.Create() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Start and Current are the machine's first fields, written by name")
	{
		Machine patrol;

		REQUIRE(patrol.machine->GetFields().size() == 2);
		CHECK(patrol.Field(startField).type == Loom::FieldType::State);
		CHECK(patrol.Field(currentField).type == Loom::FieldType::State);

		CHECK(patrol.Field(startField).Write() == "null");

		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");

		CHECK(patrol.Field(startField).Write() == "\"StateTestIdle\"");

		REQUIRE(patrol.Field(currentField).Read("\"StateTestWalk\""));
		CHECK(patrol.machine->m_current->StateType() == "StateTestWalk");

		REQUIRE(patrol.Field(currentField).Read("null"));
		CHECK(patrol.machine->m_current->StateType().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Start comes back from a scene file")
	{
		std::string text;

		{
			Machine patrol;
			patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");

			text = Loom::SceneSerializer::Serialize(patrol.scene);
		};

		Pump();

		std::string error;
		const std::unique_ptr<Loom::Scene> loaded(Loom::SceneSerializer::Deserialize(text, &error));
		Pump();

		REQUIRE(loaded != nullptr);

		StateTestMachine* machine = loaded->GetRoot().GetChildren().front()->GetComponent<StateTestMachine>();

		REQUIRE(machine != nullptr);
		CHECK(machine->m_start->StateType() == "StateTestIdle");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the machine starts on its first update, not when it is attached")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");

		CHECK_FALSE(patrol.machine->IsStarted());
		CHECK(patrol.machine->Current() == nullptr);

		patrol.machine->OnUpdate();

		CHECK(dynamic_cast<const StateTestIdle*>(patrol.machine->Current()) != nullptr);
		CHECK(patrol.machine->m_current->StateType() == "StateTestIdle");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Current set before the machine starts is where it starts")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");
		patrol.machine->m_current = Loom::StateReference::Named("StateTestWalk");

		patrol.machine->OnFieldChanged(patrol.Field(currentField));

		CHECK(patrol.machine->Current() == nullptr);

		patrol.machine->OnUpdate();

		CHECK(dynamic_cast<const StateTestWalk*>(patrol.machine->Current()) != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Current set while the machine runs enters that state")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");
		patrol.machine->OnUpdate();

		patrol.machine->m_current = Loom::StateReference::Named("StateTestWalk");
		patrol.machine->OnFieldChanged(patrol.Field(currentField));

		CHECK(dynamic_cast<const StateTestWalk*>(patrol.machine->Current()) != nullptr);

		SUBCASE("and set to nothing, runs nothing")
		{
			patrol.machine->m_current = Loom::StateReference{ };
			patrol.machine->OnFieldChanged(patrol.Field(currentField));

			CHECK(patrol.machine->IsDisabled());
			CHECK(patrol.machine->m_current->StateType().empty());
		};

		SUBCASE("and set to nothing and then a state, ends the one it set aside")
		{
			const int exits = StateTestWalk::exits;

			patrol.machine->m_current = Loom::StateReference{ };
			patrol.machine->OnFieldChanged(patrol.Field(currentField));
			patrol.machine->OnFieldChanged(patrol.Field(currentField));

			CHECK(StateTestWalk::exits == exits);

			patrol.machine->m_current = Loom::StateReference::Named("StateTestIdle");
			patrol.machine->OnFieldChanged(patrol.Field(currentField));

			CHECK(StateTestWalk::exits == exits + 1);
			CHECK_FALSE(patrol.machine->IsDisabled());
			CHECK(dynamic_cast<const StateTestIdle*>(patrol.machine->Current()) != nullptr);
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "disabling a machine that has not started leaves Current alone")
	{
		Machine patrol;
		patrol.machine->m_current = Loom::StateReference::Named("StateTestWalk");

		patrol.machine->Disable();
		patrol.machine->Enable();

		CHECK(patrol.machine->m_current->StateType() == "StateTestWalk");
	};
};

#include "doctest.h"

#include "Render Support.h"
#include "Test Support.h"

#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SerializedField.h"

#include "Utilities/Clock.h"
#include "Utilities/CustomState.h"
#include "Utilities/StateMachine.h"
#include "Utilities/StaticStateMachine.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using LoomTests::Pump;

#pragma region Helpers

namespace
{
	struct StateTestIdle final : Loom::State<StateTestIdle>
	{
	};

	// A machine is written the way a component is, and registers the same way.
	struct StateTestMachine final : Loom::StateMachine<StateTestMachine>
	{
	};

	// One with hooks of its own, written the way a component's are.
	struct StateTestHooked final : Loom::StateMachine<StateTestHooked>
	{
		void OnStart() override { starts++; };
		void OnUpdate() override { updates++; };
		void OnFixedUpdate() override { fixedUpdates++; };

		int starts = 0;
		int updates = 0;
		int fixedUpdates = 0;
	};

	struct StateTestCounted final : Loom::State<StateTestCounted>
	{
		void OnUpdate() override { updates++; };

		static inline int updates = 0;
	};

	struct StateTestFixed final : Loom::State<StateTestFixed>
	{
		void OnFixedUpdate() override { steps++; };

		static inline int steps = 0;
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

#pragma endregion


TEST_SUITE("StateMachine")
{
	#pragma region Registration

	TEST_CASE("a state machine is offered under Add Component by its own name")
	{
		CHECK(Loom::ComponentRegistry::All().contains("StateTestMachine"));
	};

	TEST_CASE("a plain StateMachine is offered under Add Component")
	{
		CHECK(Loom::ComponentRegistry::All().contains("StateMachine"));
	};

	TEST_CASE("a project's state machine is filed under Custom/StateMachines")
	{
		CHECK(Loom::ComponentRegistry::CategoryOf("StateTestMachine") == "Custom/StateMachines");
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

	#pragma endregion

	#pragma region Fields

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

	#pragma endregion

	#pragma region Running

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the machine starts on its first step, not when it is attached")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");

		CHECK_FALSE(patrol.machine->IsStarted());
		CHECK(patrol.machine->Current() == nullptr);

		patrol.machine->Step();

		CHECK(dynamic_cast<const StateTestIdle*>(patrol.machine->Current()) != nullptr);
		CHECK(patrol.machine->m_current->StateType() == "StateTestIdle");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a machine's own OnStart and OnUpdate are its, and its states still run")
	{
		Loom::Scene scene{ "Hooked" };
		Loom::GameObject* object = scene.AddChild("Guard");
		Pump();

		StateTestHooked* machine = object->Attach<StateTestHooked>();
		Pump();

		machine->m_start = Loom::StateReference::Named("StateTestCounted");

		const int stateUpdates = StateTestCounted::updates;

		// One frame of play, the way the engine runs it.
		scene.Update();
		Loom::Utilities::Update();

		CHECK(machine->starts == 1);
		CHECK(machine->updates == 1);
		CHECK(StateTestCounted::updates == stateUpdates + 1);

		scene.Update();
		Loom::Utilities::Update();

		CHECK(machine->starts == 1);
		CHECK(machine->updates == 2);
		CHECK(StateTestCounted::updates == stateUpdates + 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Current set before the machine starts is where it starts")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");
		patrol.machine->m_current = Loom::StateReference::Named("StateTestWalk");

		patrol.machine->OnFieldChanged(patrol.Field(currentField));

		CHECK(patrol.machine->Current() == nullptr);

		patrol.machine->Step();

		CHECK(dynamic_cast<const StateTestWalk*>(patrol.machine->Current()) != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Current set while the machine runs enters that state")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestIdle");
		patrol.machine->Step();

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

	#pragma endregion

	#pragma region Fixed updates

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the current state gets one fixed update for each call")
	{
		constexpr int physics_steps = 3;

		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestFixed");
		patrol.machine->Step();

		const int steps = StateTestFixed::steps;

		for (int step = 0; step < physics_steps; step++)
			Loom::Utilities::FixedUpdate();

		CHECK(StateTestFixed::steps == steps + physics_steps);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fixed update neither starts a machine nor reaches a state it has not entered")
	{
		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestFixed");

		const int steps = StateTestFixed::steps;

		Loom::Utilities::FixedUpdate();

		CHECK_FALSE(patrol.machine->IsStarted());
		CHECK(StateTestFixed::steps == steps);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a machine's own OnFixedUpdate is its, and its state still gets the step")
	{
		Loom::Scene scene{ "Hooked" };
		Loom::GameObject* object = scene.AddChild("Guard");
		Pump();

		StateTestHooked* machine = object->Attach<StateTestHooked>();
		Pump();

		machine->m_start = Loom::StateReference::Named("StateTestFixed");
		machine->Step();

		const int steps = StateTestFixed::steps;

		Loom::Utilities::FixedUpdate();
		scene.Physics();

		CHECK(machine->fixedUpdates == 1);
		CHECK(StateTestFixed::steps == steps + 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "every state running in parallel gets the fixed update")
	{
		Machine patrol;
		int steps = 0;

		const auto counted = [&steps]()
		{
			auto state = std::make_shared<Loom::CustomState>();
			state->onFixedUpdate = [&steps](Loom::CustomState&) { steps++; };

			return state;
		};

		patrol.machine->Step();
		patrol.machine->EnterParallel({ counted(), counted() });

		Loom::Utilities::FixedUpdate();

		CHECK(steps == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the static machine's state gets the fixed update")
	{
		Loom::StaticStateMachine::Destroy();
		Loom::StaticStateMachine& machine = Loom::StaticStateMachine::Instance();
		machine.startState = Loom::StateReference::Named("StateTestFixed");
		machine.Step();

		const int steps = StateTestFixed::steps;

		Loom::Utilities::FixedUpdate();

		CHECK(StateTestFixed::steps == steps + 1);

		Loom::StaticStateMachine::Destroy();
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE_FIXTURE(LoomTests::RenderFixture, "the engine gives a state a fixed update for each physics step, and none while the scenes are held")
	{
		constexpr float rate = 1000.0f;
		constexpr int frames = 8;
		constexpr std::chrono::milliseconds frame_time{ 2 };

		const float was = Loom::Physics::FixedDeltaTime();
		Loom::Physics::SetFixedDeltaTime(1.0f / rate);

		Machine patrol;
		patrol.machine->m_start = Loom::StateReference::Named("StateTestFixed");

		LoomTests::Probe<>* probe = patrol.object->Attach<LoomTests::Probe<>>();
		Pump();

		// The machine starts at the end of this one, after its steps.
		Frame();

		const int steps = StateTestFixed::steps;
		const size_t probed = probe->Count("fixed");

		for (int frame = 0; frame < frames; frame++)
		{
			std::this_thread::sleep_for(frame_time);
			Frame();
		};

		const int taken = (int)(probe->Count("fixed") - probed);

		CHECK(taken > 0);
		CHECK(StateTestFixed::steps - steps == taken);

		const int before_hold = StateTestFixed::steps;

		Loom::Engine::updateScenes = false;

		std::this_thread::sleep_for(frame_time);
		Frame();

		Loom::Engine::updateScenes = true;

		CHECK(StateTestFixed::steps == before_hold);

		Loom::Physics::SetFixedDeltaTime(was);
	};
#endif

	#pragma endregion
};

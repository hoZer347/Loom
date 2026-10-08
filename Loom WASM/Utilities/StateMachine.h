#pragma once

#include "Clock.h"
#include "State.h"
#include "StateMachineMonitor.h"
#include "StateReference.h"

#include "Component.h"
#include "Loom API.h"
#include "Serial.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>


namespace Loom
{
	/// A queue of states, the one currently running, and the frame that drives them.
	///
	/// The pump lives in the machine itself rather than in a separate core object every
	/// call forwards to, so whatever hosts a machine -- a Component, or the always-on
	/// StaticStateMachine -- simply derives this.
	struct LOOM_API StateMachineBase :
		public detail::StateCommands<StateMachineBase>
	{
		StateMachineBase();
		virtual ~StateMachineBase();

		/// Where this machine's commands land. It is its own pump.
		StateMachineBase& Machine() { return *this; };

		State* Current() const { return current.get(); };

		#pragma region Primitives

		// What StateCommands lands on. Distinct names rather than overloads of the
		// commands themselves, so it is always clear which layer a call is in: these take
		// a state that already exists, the commands build one.

		/// Queues at the front, then proceeds -- SetState.
		State* Enter(std::shared_ptr<State> state);

		/// Queues at the back -- Push.
		State* Queue(std::shared_ptr<State> state);

		/// Queues at the front -- PushFirst.
		State* QueueFirst(std::shared_ptr<State> state);

		/// Queues a set that runs at once, then proceeds -- SetParallel.
		std::vector<State*> EnterParallel(std::vector<std::shared_ptr<State>> states);

		/// Queues a set that runs at once -- PushParallel.
		std::vector<State*> QueueParallel(std::vector<std::shared_ptr<State>> states);

		/// The same, at the front -- PushParallelFirst.
		std::vector<State*> QueueParallelFirst(std::vector<std::shared_ptr<State>> states);

		#pragma endregion

		/// Takes the current state out and brings the head of the queue in. Returns what
		/// is left at the head.
		State* Proceed();

		/// Empties the queue. The current state keeps running.
		void Clear();

		/// Saves the current state aside and runs nothing until Enable.
		void Disable();

		/// Reinstates the state Disable put aside.
		void Enable();

		#pragma region Frame

		// Driven by whatever hosts the machine. Named apart from the Component hooks of
		// the same shape, since a machine on a GameObject is both.

		/// Runs OnMachineStart and enters FirstState. Safe to call twice.
		void StartMachine();

		void PumpUpdate();
		void PumpPhysics();
		void PumpLate();
		void PumpGui();

		/// One update step at a given delta, without the hook. For driving a machine by
		/// hand -- a test, or a machine ticking on something other than the frame clock.
		void Advance(float deltaTime);

		#pragma endregion

		#pragma region Inspection

		/// States waiting to run, next first. The current state is not among them.
		const std::vector<std::shared_ptr<State>>& Queued() const { return _stateQueue; };

		/// Entered states with the time they were entered, oldest first. Empty unless
		/// maxSavedStates is above zero.
		const std::deque<std::pair<float, std::shared_ptr<State>>>& History() const { return _stateStack; };

		/// True between Disable and Enable -- a state is held aside and nothing is running.
		bool IsDisabled() const { return disabledState != nullptr && current == nullptr; };

		/// Seconds this machine has been ticking, the clock the history is stamped against.
		float ElapsedTime() const { return elapsedTime; };

		/// What a parallel state is running at once, or null for an ordinary state.
		static const std::vector<std::shared_ptr<State>>* SubStatesOf(const State* state);

		/// Writes every recorded past state (oldest first) out as text, and clears it.
		std::string FlushStateHistory();

		#pragma endregion

		/// True once StartMachine has run.
		bool IsStarted() const { return started; };

		/// How much state history to keep before discarding the oldest. Keep it low or
		/// zero unless debugging, to avoid memory over-use.
		int maxSavedStates = 0;

		/// The object the machine is attached to, or null for one that is not on an object.
		virtual GameObject* GetGameObject() { return nullptr; };

		/// What the monitor lists the machine under.
		virtual std::string MachineName() const { return "StateMachine"; };

		/// Every machine currently alive, for the monitor.
		static std::vector<StateMachineBase*> All();

	protected:
		#pragma region Subclass hooks

		// Deliberately not named OnStart, OnUpdate and so on. A machine on a GameObject
		// is also a Component, whose OnUpdate and OnPhysics are virtuals of the same
		// signature; one override in the subclass would silently override both, and the
		// pump's call to its own hook would land back in the component and recurse until
		// the stack ran out.

		virtual void OnMachineStart() { };
		virtual void OnMachineUpdate() { };
		virtual void OnMachinePhysics() { };
		virtual void OnMachineLateUpdate() { };
		virtual void OnMachineGui() { };

		/// After the current state changes, whether by Proceed, Disable or Enable.
		virtual void OnMachineStateChanged() { };

		#pragma endregion

		/// The state the machine enters when it starts, or null to start on nothing.
		virtual std::shared_ptr<State> FirstState() const { return nullptr; };

	private:
		struct St_Parallel;

		/// Wraps a set of states in one parallel state, and hands back the raw pointers.
		/// Both parallel queues differ only in which end the result goes on.
		static std::vector<State*> Parallelize(
			std::vector<std::shared_ptr<State>> states,
			std::shared_ptr<State>& out);

		void Adopt(const std::shared_ptr<State>& state);

		std::shared_ptr<State> current{ };
		std::shared_ptr<State> disabledState{ };

		float elapsedTime = 0.0f;
		bool started = false;

		/// Queue of states. On Proceed, pulls the first element.
		std::vector<std::shared_ptr<State>> _stateQueue{ };

		/// Previously entered states, for debugging.
		std::deque<std::pair<float, std::shared_ptr<State>>> _stateStack{ };
	};

	// Now that the machine is a complete type, the two commands State could not carry
	// inline.
	inline void State::Proceed() { stateMachine->Proceed(); };
	inline void State::Clear() { stateMachine->Clear(); };

	/// A state machine living on a GameObject, with its Start and Current states in
	/// the inspector and the scene file.
	///
	/// Self-typed, so StateOf&lt;_Focus&gt; resolves to the concrete machine. The engine
	/// requires it in any case:
	/// Component&lt;T&gt; is CRTP and GameObject::Attach refuses anything that is not a
	/// component of its own type, so a machine subclass has to name itself here regardless.
	///
	///		struct Camp : Loom::StateMachineOf&lt;Camp&gt; { };
	///		gameObject->Attach&lt;Camp&gt;();
	template <typename _Self>
	struct StateMachineOf :
		public Component<_Self>,
		public StateMachineBase
	{
		/// What the machine enters when play starts, unless Current names a state.
		Serial<StateReference> m_start;

		/// The state running now. Setting it enters that state, or, before the machine
		/// has started, is where it will start.
		Serial<StateReference> m_current;

		GameObject* GetGameObject() override { return this->m_gameObject; };

		std::string MachineName() const override { return typeid(_Self).name(); };

		// Public because Loom's GameObject::Attach asks &T::OnAttach and friends from
		// outside the class to work out which of them a component overrides.

		void OnUpdate() override
		{
			// Started here rather than on attach: attaching happens in the editor
			// too, before the scene has read Start in, and a machine is only meant
			// to run while the scene does.
			StartMachine();

			// GameObject never fills m_physicsables, so the engine does not dispatch
			// ComponentBase::OnPhysics and a state's OnPhysics would never run. Pump the
			// physics step from here until it does; the flag below makes the changeover
			// automatic once the engine starts dispatching.
			if (!_physics_dispatched)
				PumpPhysics();

			PumpUpdate();
			PumpLate();
		};

		void OnPhysics() override
		{
			_physics_dispatched = true;

			PumpPhysics();
		};

		void OnGui() override
		{
			// The queue, the history and the state it is on, over the live machine.
			StateMachineMonitor::DrawInline(*this);

			PumpGui();
		};

		void OnFieldChanged(const SerializedField& field) override
		{
			if (field.data != &*m_current || !IsStarted())
				return;

			if (std::shared_ptr<State> state = m_current->Create())
				Enter(std::move(state));
			else Disable();
		};

	protected:
		std::shared_ptr<State> FirstState() const override
		{
			std::shared_ptr<State> first = m_current->Create();

			return first ? first : m_start->Create();
		};

		void OnMachineStateChanged() override
		{
			const State* state = Current();

			m_current = state
				? StateReference::Named(PrettyTypeName(typeid(*state).name()))
				: StateReference{ };
		};

	private:
		bool _physics_dispatched = false;
	};

	/// A machine with no behaviour of its own, for when the states are the whole of it.
	struct StateMachine final : StateMachineOf<StateMachine>
	{
	};
};

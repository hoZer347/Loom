#pragma once

#include "Clock.h"
#include "State.h"
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

		StateBase* Current() const { return current.get(); };

		#pragma region Primitives

		// What StateCommands lands on. Distinct names rather than overloads of the
		// commands themselves, so it is always clear which layer a call is in: these take
		// a state that already exists, the commands build one.

		/// Queues at the front, then proceeds -- SetState.
		StateBase* Enter(std::shared_ptr<StateBase> state);

		/// Queues at the back -- Push.
		StateBase* Queue(std::shared_ptr<StateBase> state);

		/// Queues at the front -- PushFirst.
		StateBase* QueueFirst(std::shared_ptr<StateBase> state);

		/// Queues a set that runs at once, then proceeds -- SetParallel.
		std::vector<StateBase*> EnterParallel(std::vector<std::shared_ptr<StateBase>> states);

		/// Queues a set that runs at once -- PushParallel.
		std::vector<StateBase*> QueueParallel(std::vector<std::shared_ptr<StateBase>> states);

		/// The same, at the front -- PushParallelFirst.
		std::vector<StateBase*> QueueParallelFirst(std::vector<std::shared_ptr<StateBase>> states);

		#pragma endregion

		/// Takes the current state out and brings the head of the queue in. Returns what
		/// is left at the head.
		StateBase* Proceed();

		/// Empties the queue. The current state keeps running.
		void Clear();

		/// Saves the current state aside and runs nothing until Enable.
		void Disable();

		/// Reinstates the state Disable put aside.
		void Enable();

		#pragma region Frame

		// Driven by the engine rather than by the hooks of the component a machine is,
		// so a machine's own OnUpdate and the rest are free for it to write.

		/// Runs OnStart and enters FirstState. Safe to call twice.
		void StartMachine();

		/// One frame: starts the machine if it has not started, then runs the current
		/// state's physics, update and late update.
		void Step();

		/// Steps every machine on a GameObject. What the engine runs each frame the
		/// scenes update.
		static void StepAttached();

		/// The current state's debug GUI, for whatever draws the machine.
		void PumpGui();

		/// The machine's rows, then its current state's debug GUI.
		void DrawMachineGui();

		/// The current state's update at a given delta. For driving a machine by hand -- a
		/// test, or a machine ticking on something other than the frame clock.
		void Advance(float deltaTime);

		#pragma endregion

		#pragma region Inspection

		/// States waiting to run, next first. The current state is not among them.
		const std::vector<std::shared_ptr<StateBase>>& Queued() const { return _stateQueue; };

		/// Entered states with the time they were entered, oldest first. Empty unless
		/// maxSavedStates is above zero.
		const std::deque<std::pair<float, std::shared_ptr<StateBase>>>& History() const { return _stateStack; };

		/// True between Disable and Enable -- a state is held aside and nothing is running.
		bool IsDisabled() const { return disabledState != nullptr && current == nullptr; };

		/// Seconds this machine has been ticking, the clock the history is stamped against.
		float ElapsedTime() const { return elapsedTime; };

		/// What a parallel state is running at once, or null for an ordinary state.
		static const std::vector<std::shared_ptr<StateBase>>* SubStatesOf(const StateBase* state);

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

		/// When the machine starts, before it enters its first state. A machine on a
		/// GameObject starts on the first frame of play.
		virtual void OnStart() { };

	protected:
		/// After the current state changes, whether by Proceed, Disable or Enable.
		virtual void OnMachineStateChanged() { };

		/// The state the machine enters when it starts, or null to start on nothing.
		virtual std::shared_ptr<StateBase> FirstState() const { return nullptr; };

		/// Ends the state Disable put aside, for when another is entered in its place
		/// rather than it being enabled again.
		void ExitDisabled();

	private:
		struct St_Parallel;

		/// Wraps a set of states in one parallel state, and hands back the raw pointers.
		/// Both parallel queues differ only in which end the result goes on.
		static std::vector<StateBase*> Parallelize(
			std::vector<std::shared_ptr<StateBase>> states,
			std::shared_ptr<StateBase>& out);

		void Adopt(const std::shared_ptr<StateBase>& state);

		std::shared_ptr<StateBase> current{ };
		std::shared_ptr<StateBase> disabledState{ };

		float elapsedTime = 0.0f;
		bool started = false;

		/// Queue of states. On Proceed, pulls the first element.
		std::vector<std::shared_ptr<StateBase>> _stateQueue{ };

		/// Previously entered states, for debugging.
		std::deque<std::pair<float, std::shared_ptr<StateBase>>> _stateStack{ };
	};

	// Now that the machine is a complete type, the two commands StateBase could not carry
	// inline.
	inline void StateBase::Proceed() { stateMachine->Proceed(); };
	inline void StateBase::Clear() { stateMachine->Clear(); };

	/// A state machine living on a GameObject, with its Start and Current states in
	/// the inspector and the scene file.
	///
	/// Written like a component: OnAttach, OnUpdate and the rest are its own, and OnStart
	/// runs when play starts. The engine steps its states each frame of play.
	///
	/// Self-typed, so StateOf&lt;_Focus&gt; resolves to the concrete machine. The engine
	/// requires it in any case:
	/// Component&lt;T&gt; is CRTP and GameObject::Attach refuses anything that is not a
	/// component of its own type, so a machine subclass has to name itself here regardless.
	///
	///		struct Camp : Loom::StateMachine&lt;Camp&gt; { };
	///		gameObject->Attach&lt;Camp&gt;();
	template <typename _Self>
	struct StateMachine :
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

		void ExtraGui() override { DrawMachineGui(); };

		void OnFieldChanged(const SerializedField& field) override
		{
			if (field.data != &*m_current || !IsStarted())
				return;

			if (std::shared_ptr<StateBase> state = m_current->Create())
			{
				ExitDisabled();
				Enter(std::move(state));
			}
			else if (!IsDisabled())
				Disable();
		};

	protected:
		std::shared_ptr<StateBase> FirstState() const override
		{
			std::shared_ptr<StateBase> first = m_current->Create();

			return first ? first : m_start->Create();
		};

		void OnMachineStateChanged() override
		{
			// Before the machine runs, Current is what the inspector set, which a
			// Disable in edit mode is not to wipe.
			if (!IsStarted())
				return;

			const StateBase* state = Current();

			m_current = state
				? StateReference::Named(PrettyTypeName(typeid(*state).name()))
				: StateReference{ };
		};
	};

	namespace Basic
	{
		/// A machine with no behaviour of its own, for when the states are the whole of
		/// it. In a namespace of its own so it can be called StateMachine beside the
		/// template, and the registry, which drops namespaces, offers it as that.
		struct StateMachine final : Loom::StateMachine<StateMachine>
		{
		};
	};
};

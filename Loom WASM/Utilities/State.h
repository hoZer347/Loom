#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct State;
	struct StateMachineBase;

	/// Anything a machine will run.
	///
	/// A concept rather than the static_assert every one of these templates used to open
	/// with: the constraint is part of the signature now, so a wrong type is rejected at
	/// the call rather than inside the body, and it is written once instead of nine times.
	template <typename _Type>
	concept AState = std::derived_from<_Type, State>;

	namespace detail
	{
		/// Every command that puts a state into a machine, written once.
		///
		/// Two things offer this surface: a machine, because it is its API, and a state,
		/// so a state can say Push<Foo>() rather than stateMachine.Push<Foo>(). Declared
		/// once here, because copies of one list only have to disagree once.
		///
		/// Whoever mixes it in says which pump the commands land on, through Machine().
		/// Everything here is a template, so the machine only has to be a complete type
		/// where a command is actually called -- which is what lets State carry this
		/// without knowing yet what a StateMachineBase is.
		template <typename _Self>
		struct StateCommands
		{
			/// Queues a state at the front and proceeds straight to it.
			template <AState _State, typename... _Args>
			_State* SetState(_Args&&... args)
			{
				return static_cast<_State*>(
					Pump().Enter(std::make_shared<_State>(std::forward<_Args>(args)...)));
			};

			/// The same, for a state that has already been built and configured.
			template <AState _State>
			_State* SetState(std::shared_ptr<_State> state)
			{
				return static_cast<_State*>(Pump().Enter(std::move(state)));
			};

			/// Queues a state at the back.
			template <AState _State, typename... _Args>
			_State* Push(_Args&&... args)
			{
				return static_cast<_State*>(
					Pump().Queue(std::make_shared<_State>(std::forward<_Args>(args)...)));
			};

			template <AState _State>
			_State* Push(std::shared_ptr<_State> state)
			{
				return static_cast<_State*>(Pump().Queue(std::move(state)));
			};

			/// Queues a state at the front, so it is the next one to run.
			template <AState _State, typename... _Args>
			_State* PushFirst(_Args&&... args)
			{
				return static_cast<_State*>(
					Pump().QueueFirst(std::make_shared<_State>(std::forward<_Args>(args)...)));
			};

			template <AState _State>
			_State* PushFirst(std::shared_ptr<_State> state)
			{
				return static_cast<_State*>(Pump().QueueFirst(std::move(state)));
			};

			/// Queues a number of states that then all run at once, and proceeds to them.
			auto SetParallel(std::vector<std::shared_ptr<State>> states)
			{
				return Pump().EnterParallel(std::move(states));
			};

			/// The same, without proceeding.
			auto PushParallel(std::vector<std::shared_ptr<State>> states)
			{
				return Pump().QueueParallel(std::move(states));
			};

			/// The same again, at the front of the queue.
			auto PushParallelFirst(std::vector<std::shared_ptr<State>> states)
			{
				return Pump().QueueParallelFirst(std::move(states));
			};

		private:
			// auto return types above, so these three are deduced when they are called
			// rather than when the class is instantiated -- the same laziness the
			// templates get for free, and the reason none of this needs the machine
			// declared yet.
			StateMachineBase& Pump() { return static_cast<_Self&>(*this).Machine(); };
		};
	};

	/// One step of a machine's behaviour.
	///
	/// States are held by shared_ptr: the history keeps entered states alive well after
	/// the machine has moved on, so ownership is shared between it and the queue.
	struct State :
		protected detail::StateCommands<State>
	{
		virtual ~State() = default;

		/// The object the owning machine is attached to. Null on a machine that is not on
		/// one -- the StaticStateMachine, or a machine driven by hand.
		GameObject* gameObject = nullptr;

		StateMachineBase* stateMachine = nullptr;

		/// Where this state's commands land. What StateCommands asks of whoever mixes it in.
		StateMachineBase& Machine() const { return *stateMachine; };

		#pragma region Overrideables

		/// Called when Proceed brings this state in -- the equivalent of a start function.
		virtual void OnEnter(State* lastState) { };

		/// Called every frame the state is current.
		virtual void OnUpdate() { };

		/// Called from the machine's physics step.
		virtual void OnPhysics() { };

		/// Called after all update work, so follow/camera logic reads the final state of
		/// the frame.
		virtual void OnLateUpdate() { };

		/// Called from the machine's ImGui pass, which is where a state draws its debug;
		/// there is no scene view.
		virtual void OnGui() { };

		/// Called when Proceed takes this state out.
		virtual void OnExit(State* nextState) { };

		#pragma endregion

		/// What the monitor and the history call this state. typeid gives the concrete
		/// type even through a State*.
		virtual std::string Name() const { return typeid(*this).name(); };

		/// Anything extra worth writing into the flushed history. A state that wants its
		/// own fields in the log says so here.
		virtual std::string Describe() const { return ""; };

	protected:
		friend struct detail::StateCommands<State>;

		// The two commands that are not about queueing a state, and so cannot be
		// templates. Defined in StateMachine.h, where the machine is a complete type.
		void Proceed();
		void Clear();
	};

	/// A state that knows what kind of machine it is running on, and reaches it through
	/// Focus(). Spelled StateOf rather than State, a class and a class template not
	/// being able to share a name.
	template <typename _Focus>
	struct StateOf : State
	{
	protected:
		_Focus* Focus() const { return static_cast<_Focus*>(stateMachine); };
	};

	/// Builds a vector of states for the parallel commands, without the caller spelling
	/// out make_shared each time: PushParallel(States<St_Walk, St_Talk>()).
	template <AState... _States>
	std::vector<std::shared_ptr<State>> States()
	{
		return { std::static_pointer_cast<State>(std::make_shared<_States>())... };
	};
};

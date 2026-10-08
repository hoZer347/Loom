#pragma once

#include "ComponentRegistry.h"
#include "Loom API.h"

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct StateBase;
	struct StateMachineBase;

	/// Anything a machine will run.
	///
	/// A concept rather than the static_assert every one of these templates used to open
	/// with: the constraint is part of the signature now, so a wrong type is rejected at
	/// the call rather than inside the body, and it is written once instead of nine times.
	template <typename _Type>
	concept AState = std::derived_from<_Type, StateBase>;

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
		/// where a command is actually called -- which is what lets StateBase carry this
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
			auto SetParallel(std::vector<std::shared_ptr<StateBase>> states)
			{
				return Pump().EnterParallel(std::move(states));
			};

			/// The same, without proceeding.
			auto PushParallel(std::vector<std::shared_ptr<StateBase>> states)
			{
				return Pump().QueueParallel(std::move(states));
			};

			/// The same again, at the front of the queue.
			auto PushParallelFirst(std::vector<std::shared_ptr<StateBase>> states)
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
	struct StateBase :
		protected detail::StateCommands<StateBase>
	{
		virtual ~StateBase() = default;

		/// The object the owning machine is attached to. Null on a machine that is not on
		/// one -- the StaticStateMachine, or a machine driven by hand.
		GameObject* gameObject = nullptr;

		StateMachineBase* stateMachine = nullptr;

		/// Where this state's commands land. What StateCommands asks of whoever mixes it in.
		StateMachineBase& Machine() const { return *stateMachine; };

		#pragma region Overrideables

		/// Called when Proceed brings this state in -- the equivalent of a start function.
		virtual void OnEnter(StateBase* lastState) { };

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
		virtual void OnExit(StateBase* nextState) { };

		#pragma endregion

		/// What the monitor and the history call this state. typeid gives the concrete
		/// type even through a StateBase*.
		virtual std::string Name() const { return typeid(*this).name(); };

		/// Anything extra worth writing into the flushed history. A state that wants its
		/// own fields in the log says so here.
		virtual std::string Describe() const { return ""; };

	protected:
		friend struct detail::StateCommands<StateBase>;

		// The two commands that are not about queueing a state, and so cannot be
		// templates. Defined in StateMachine.h, where the machine is a complete type.
		void Proceed();
		void Clear();
	};

	/// A state that knows what kind of machine it is running on, and reaches it through
	/// Focus(). Not registered: for states a machine builds itself, as the dialogue's
	/// are, which have no business in a Start or Current dropdown.
	template <typename _Focus>
	struct StateOf : StateBase
	{
	protected:
		_Focus* Focus() const { return static_cast<_Focus*>(stateMachine); };
	};

	/// Names that can be built by string. A state registers itself once, and anything
	/// data-driven (a monitor's "set state" box, a config file, a script) can then
	/// reach it.
	struct LOOM_API StateRegistry final
	{
		static void Register(const std::string& name, std::function<std::shared_ptr<StateBase>()> create);

		/// Takes a name back out: a script library is unloaded before it is rebuilt,
		/// and a factory pointing into code that is no longer mapped is a crash.
		static void Unregister(const std::string& name);

		static std::shared_ptr<StateBase> Create(const std::string& name);

		static bool Contains(const std::string& name);

		/// Every registered name, sorted, for a dropdown to list.
		static std::vector<std::string> Names();

		StateRegistry() = delete;
	};

	/// A state that registers itself under its own type name, so a machine's Start and
	/// Current can name it, and that reaches its machine through Focus(). The state
	/// equivalent of Component&lt;T&gt;: self-typed, and nothing else to write.
	///
	///		struct St_Idle : Loom::State&lt;St_Idle&gt; { };
	///		struct St_Walk : Loom::State&lt;St_Walk, Camp&gt; { };
	template <typename _Self, typename _Focus = StateMachineBase>
	struct State : StateOf<_Focus>
	{
	private:
		// Generic over the type rather than naming _Self in the class, for the same
		// reason as Component&lt;T&gt;'s: _Self is not complete until it has derived from
		// this.
		template <typename _Type>
		static bool Register()
		{
			if constexpr (std::is_default_constructible_v<_Type>)
				StateRegistry::Register(
					PrettyTypeName(typeid(_Type).name()),
					[]() -> std::shared_ptr<StateBase> { return std::make_shared<_Type>(); });

			return true;
		};

		static inline const bool s_registered = Register<_Self>();

		// A static member is only defined once something uses it, and nothing in
		// _Self would. Naming it in a typedef does.
		typedef std::integral_constant<const bool*, &s_registered> Registration;
	};

	/// Builds a vector of states for the parallel commands, without the caller spelling
	/// out make_shared each time: PushParallel(States<St_Walk, St_Talk>()).
	template <AState... _States>
	std::vector<std::shared_ptr<StateBase>> States()
	{
		return { std::static_pointer_cast<StateBase>(std::make_shared<_States>())... };
	};
};

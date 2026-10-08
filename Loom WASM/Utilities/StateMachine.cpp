#include "StateMachine.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <ranges>
#include <sstream>


namespace Loom
{
	#pragma region StateRegistry

	static std::map<std::string, std::function<std::shared_ptr<StateBase>()>>& Registry()
	{
		// Function-local, so a state registering itself from a static initialiser in
		// another translation unit cannot beat the map into existence.
		static std::map<std::string, std::function<std::shared_ptr<StateBase>()>> registry;

		return registry;
	};

	void StateRegistry::Register(const std::string& name, std::function<std::shared_ptr<StateBase>()> create)
	{
		Registry()[name] = std::move(create);
	};

	void StateRegistry::Unregister(const std::string& name)
	{
		Registry().erase(name);
	};

	std::shared_ptr<StateBase> StateRegistry::Create(const std::string& name)
	{
		const auto found = Registry().find(name);

		return found == Registry().end() ? nullptr : found->second();
	};

	bool StateRegistry::Contains(const std::string& name)
	{
		return Registry().contains(name);
	};

	std::vector<std::string> StateRegistry::Names()
	{
		const auto keys = Registry() | std::views::keys;

		return std::vector<std::string>(keys.begin(), keys.end());
	};

	StateReference StateReference::Named(const std::string& name)
	{
		StateReference reference;

		reference._type_name = name;

		return reference;
	};

	bool StateReference::IsSet() const
	{
		return _create || StateRegistry::Contains(_type_name);
	};

	std::shared_ptr<StateBase> StateReference::Create() const
	{
		return _create
			? _create()
			: StateRegistry::Create(_type_name);
	};

	#pragma endregion

	#pragma region ParallelState

	/// Handler for many sub-states running at the same time: does not proceed until every
	/// sub-state inside it has triggered Proceed. (Sub-states still run their own OnExit
	/// as they proceed, rather than all at once at the end.)
	struct StateMachineBase::St_Parallel final : StateBase
	{
		/// Sub-states that all run at once.
		std::vector<std::shared_ptr<StateBase>> SubStates{ };

		/// Currently active sub-state, so a Proceed from inside one is attributed to it.
		StateBase* _active = nullptr;

		std::string Name() const override { return "St_Parallel"; };

		/// Handles an individual sub-state calling Proceed. True while others remain.
		bool OnChildProceed()
		{
			const auto found = std::ranges::find_if(
				SubStates,
				[this](const auto& state) { return state.get() == _active; });

			if (_active == nullptr || found == SubStates.end())
				return false;

			const std::shared_ptr<StateBase> finished = *found;

			SubStates.erase(found);

			_active = nullptr;

			finished->OnExit(nullptr);

			return !SubStates.empty();
		};

		/// Runs one step over every sub-state, keeping track of which one is running so a
		/// Proceed from inside it can be attributed. Copied first, because a sub-state is
		/// allowed to proceed from inside the very step being walked.
		void Each(auto&& step)
		{
			for (const auto& state : std::vector<std::shared_ptr<StateBase>>(SubStates))
			{
				_active = state.get();

				if (state)
					step(*state);

				_active = nullptr;
			};
		};

		void OnEnter(StateBase* lastState) override { Each([&](StateBase& s) { s.OnEnter(lastState); }); };
		void OnUpdate() override { Each([](StateBase& s) { s.OnUpdate(); }); };
		void OnPhysics() override { Each([](StateBase& s) { s.OnPhysics(); }); };
		void OnLateUpdate() override { Each([](StateBase& s) { s.OnLateUpdate(); }); };
		void OnGui() override { Each([](StateBase& s) { s.OnGui(); }); };

		// The one step that is not attributed to a sub-state: nothing is going to proceed
		// out of a parallel that is already on its way out.
		void OnExit(StateBase* nextState) override
		{
			for (const auto& state : SubStates)
				if (state)
					state->OnExit(nextState);
		};
	};

	const std::vector<std::shared_ptr<StateBase>>* StateMachineBase::SubStatesOf(const StateBase* state)
	{
		const St_Parallel* parallel = dynamic_cast<const St_Parallel*>(state);

		return parallel ? &parallel->SubStates : nullptr;
	};

	#pragma endregion

	#pragma region Lifetime

	namespace
	{
		// Here rather than static members of an exported class, which a script
		// module importing it could not define. Function-local, like the
		// registry, so a machine built by a static initialiser finds them.
		std::vector<StateMachineBase*>& Machines()
		{
			static std::vector<StateMachineBase*> machines{ };
			return machines;
		};

		std::recursive_mutex& MachinesMutex()
		{
			static std::recursive_mutex mutex{ };
			return mutex;
		};
	};

	StateMachineBase::StateMachineBase()
	{
		std::scoped_lock lock(MachinesMutex());

		Machines().push_back(this);
	};

	StateMachineBase::~StateMachineBase()
	{
		std::scoped_lock lock(MachinesMutex());

		std::erase(Machines(), this);
	};

	std::vector<StateMachineBase*> StateMachineBase::All()
	{
		std::scoped_lock lock(MachinesMutex());

		return Machines();
	};

	#pragma endregion

	#pragma region Frame

	void StateMachineBase::StartMachine()
	{
		if (started)
			return;

		started = true;

		OnStart();

		if (std::shared_ptr<StateBase> first = FirstState())
			Enter(std::move(first));
	};

	void StateMachineBase::Step()
	{
		StartMachine();

		if (current)
			current->OnPhysics();

		Advance(Time::DeltaTime());

		if (current)
			current->OnLateUpdate();
	};

	void StateMachineBase::StepAttached()
	{
		const std::vector<StateMachineBase*> machines = All();

		for (StateMachineBase* machine : machines)
		{
			// A state may destroy a machine, its own or another, mid-frame.
			const std::vector<StateMachineBase*> alive = All();

			if (std::ranges::find(alive, machine) != alive.end() && machine->GetGameObject())
				machine->Step();
		};
	};

	void StateMachineBase::Advance(float deltaTime)
	{
		elapsedTime += deltaTime;

		if (current)
			current->OnUpdate();
	};

	void StateMachineBase::PumpGui()
	{
		if (current)
			current->OnGui();
	};

	#pragma endregion

	#pragma region State Management

	void StateMachineBase::Adopt(const std::shared_ptr<StateBase>& state)
	{
		if (!state)
			return;

		state->gameObject = GetGameObject();
		state->stateMachine = this;
	};

	StateBase* StateMachineBase::Enter(std::shared_ptr<StateBase> state)
	{
		StateBase* raw = QueueFirst(std::move(state));

		Proceed();

		return raw;
	};

	StateBase* StateMachineBase::Queue(std::shared_ptr<StateBase> state)
	{
		StateBase* raw = state.get();

		_stateQueue.emplace_back(std::move(state));

		return raw;
	};

	StateBase* StateMachineBase::QueueFirst(std::shared_ptr<StateBase> state)
	{
		StateBase* raw = state.get();

		_stateQueue.insert(_stateQueue.begin(), std::move(state));

		return raw;
	};

	std::vector<StateBase*> StateMachineBase::Parallelize(
		std::vector<std::shared_ptr<StateBase>> states,
		std::shared_ptr<StateBase>& out)
	{
		auto parallel = std::make_shared<St_Parallel>();

		std::vector<StateBase*> raw;
		raw.reserve(states.size());

		for (const auto& state : states)
			raw.push_back(state.get());

		parallel->SubStates = std::move(states);

		out = std::move(parallel);

		return raw;
	};

	std::vector<StateBase*> StateMachineBase::QueueParallel(std::vector<std::shared_ptr<StateBase>> states)
	{
		std::shared_ptr<StateBase> parallel;

		std::vector<StateBase*> raw = Parallelize(std::move(states), parallel);

		_stateQueue.emplace_back(std::move(parallel));

		return raw;
	};

	std::vector<StateBase*> StateMachineBase::QueueParallelFirst(std::vector<std::shared_ptr<StateBase>> states)
	{
		std::shared_ptr<StateBase> parallel;

		std::vector<StateBase*> raw = Parallelize(std::move(states), parallel);

		_stateQueue.insert(_stateQueue.begin(), std::move(parallel));

		return raw;
	};

	std::vector<StateBase*> StateMachineBase::EnterParallel(std::vector<std::shared_ptr<StateBase>> states)
	{
		std::vector<StateBase*> raw = QueueParallel(std::move(states));

		Proceed();

		return raw;
	};

	StateBase* StateMachineBase::Proceed()
	{
		// St_Parallel overrides normal Proceed: one sub-state finishing is not the whole
		// parallel state finishing.
		if (St_Parallel* parallel = dynamic_cast<St_Parallel*>(current.get()))
			if (parallel->OnChildProceed())
				return _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		StateBase* nextState = _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		// Held, so the outgoing state survives its own OnExit and the incoming state's
		// OnEnter -- which is handed a pointer to it.
		const std::shared_ptr<StateBase> previousState = current;

		if (current)
			current->OnExit(nextState);

		if (!_stateQueue.empty())
		{
			if (maxSavedStates > 0)
			{
				_stateStack.emplace_back(elapsedTime, _stateQueue.front());

				while (std::cmp_greater(_stateStack.size(), maxSavedStates))
					_stateStack.pop_front();
			};

			current = _stateQueue.front();

			// Taken off the queue before OnEnter, not after: a state that pushes to the
			// front of the queue from inside its own OnEnter must not have that new
			// state erased in place of itself.
			_stateQueue.erase(_stateQueue.begin());

			Adopt(current);

			if (St_Parallel* parallel = dynamic_cast<St_Parallel*>(current.get()))
				for (const auto& state : parallel->SubStates)
					Adopt(state);

			OnMachineStateChanged();

			current->OnEnter(previousState.get());
		};

		return _stateQueue.empty() ? nullptr : _stateQueue.front().get();
	};

	void StateMachineBase::Clear()
	{
		_stateQueue.clear();
	};

	void StateMachineBase::Disable()
	{
		disabledState = current;
		current = nullptr;

		OnMachineStateChanged();
	};

	void StateMachineBase::ExitDisabled()
	{
		if (!IsDisabled())
			return;

		disabledState->OnExit(nullptr);
		disabledState = nullptr;
	};

	void StateMachineBase::Enable()
	{
		current = disabledState;

		OnMachineStateChanged();
	};

	#pragma endregion

	#pragma region Logging

	std::string StateMachineBase::FlushStateHistory()
	{
		std::ostringstream builder;

		builder << std::fixed;
		builder.precision(3);

		for (const auto& [time, state] : _stateStack)
		{
			if (!state)
				continue;

			builder << '[' << time << "] " << state->Name();

			if (const std::string described = state->Describe(); !described.empty())
				builder << ": " << described;

			builder << '\n';
		};

		_stateStack.clear();

		return builder.str();
	};

	#pragma endregion
};

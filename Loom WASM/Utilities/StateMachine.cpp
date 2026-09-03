#include "StateMachine.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <sstream>


namespace Loom
{
	#pragma region StateRegistry

	static std::map<std::string, std::function<std::shared_ptr<State>()>>& Registry()
	{
		// Function-local, so a state registering itself from a static initialiser in
		// another translation unit cannot beat the map into existence.
		static std::map<std::string, std::function<std::shared_ptr<State>()>> registry;

		return registry;
	};

	void StateRegistry::Register(const std::string& name, std::function<std::shared_ptr<State>()> create)
	{
		Registry()[name] = std::move(create);
	};

	std::shared_ptr<State> StateRegistry::Create(const std::string& name)
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

		if (!StateRegistry::Contains(name))
			return reference;

		reference._type_name = name;
		reference._create = [name]() { return StateRegistry::Create(name); };

		return reference;
	};

	#pragma endregion

	#pragma region ParallelState

	/// Handler for many sub-states running at the same time: does not proceed until every
	/// sub-state inside it has triggered Proceed. (Sub-states still run their own OnExit
	/// as they proceed, rather than all at once at the end.)
	struct StateMachineBase::St_Parallel final : State
	{
		/// Sub-states that all run at once.
		std::vector<std::shared_ptr<State>> SubStates{ };

		/// Currently active sub-state, so a Proceed from inside one is attributed to it.
		State* _active = nullptr;

		std::string Name() const override { return "St_Parallel"; };

		/// Handles an individual sub-state calling Proceed. True while others remain.
		bool OnChildProceed()
		{
			const auto found = std::ranges::find_if(
				SubStates,
				[this](const auto& state) { return state.get() == _active; });

			if (_active == nullptr || found == SubStates.end())
				return false;

			const std::shared_ptr<State> finished = *found;

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
			for (const auto& state : std::vector<std::shared_ptr<State>>(SubStates))
			{
				_active = state.get();

				if (state)
					step(*state);

				_active = nullptr;
			};
		};

		void OnEnter(State* lastState) override { Each([&](State& s) { s.OnEnter(lastState); }); };
		void OnUpdate() override { Each([](State& s) { s.OnUpdate(); }); };
		void OnPhysics() override { Each([](State& s) { s.OnPhysics(); }); };
		void OnLateUpdate() override { Each([](State& s) { s.OnLateUpdate(); }); };
		void OnGui() override { Each([](State& s) { s.OnGui(); }); };

		// The one step that is not attributed to a sub-state: nothing is going to proceed
		// out of a parallel that is already on its way out.
		void OnExit(State* nextState) override
		{
			for (const auto& state : SubStates)
				if (state)
					state->OnExit(nextState);
		};
	};

	const std::vector<std::shared_ptr<State>>* StateMachineBase::SubStatesOf(const State* state)
	{
		const St_Parallel* parallel = dynamic_cast<const St_Parallel*>(state);

		return parallel ? &parallel->SubStates : nullptr;
	};

	#pragma endregion

	#pragma region Lifetime

	StateMachineBase::StateMachineBase()
	{
		std::scoped_lock lock(_all_mutex);

		_all.push_back(this);
	};

	StateMachineBase::~StateMachineBase()
	{
		std::scoped_lock lock(_all_mutex);

		std::erase(_all, this);
	};

	std::vector<StateMachineBase*> StateMachineBase::All()
	{
		std::scoped_lock lock(_all_mutex);

		return _all;
	};

	#pragma endregion

	#pragma region Frame

	void StateMachineBase::StartMachine()
	{
		if (started)
			return;

		started = true;

		OnMachineStart();

		if (std::shared_ptr<State> first = startState.Create())
			Enter(std::move(first));
	};

	void StateMachineBase::Advance(float deltaTime)
	{
		elapsedTime += deltaTime;

		if (current)
			current->OnUpdate();
	};

	void StateMachineBase::PumpUpdate()
	{
		OnMachineUpdate();

		Advance(Time::DeltaTime());
	};

	void StateMachineBase::PumpPhysics()
	{
		OnMachinePhysics();

		if (current)
			current->OnPhysics();
	};

	void StateMachineBase::PumpLate()
	{
		OnMachineLateUpdate();

		if (current)
			current->OnLateUpdate();
	};

	void StateMachineBase::PumpGui()
	{
		OnMachineGui();

		if (current)
			current->OnGui();
	};

	#pragma endregion

	#pragma region State Management

	void StateMachineBase::Adopt(const std::shared_ptr<State>& state)
	{
		if (!state)
			return;

		state->gameObject = GetGameObject();
		state->stateMachine = this;
	};

	State* StateMachineBase::Enter(std::shared_ptr<State> state)
	{
		State* raw = QueueFirst(std::move(state));

		Proceed();

		return raw;
	};

	State* StateMachineBase::Queue(std::shared_ptr<State> state)
	{
		State* raw = state.get();

		_stateQueue.emplace_back(std::move(state));

		return raw;
	};

	State* StateMachineBase::QueueFirst(std::shared_ptr<State> state)
	{
		State* raw = state.get();

		_stateQueue.insert(_stateQueue.begin(), std::move(state));

		return raw;
	};

	std::vector<State*> StateMachineBase::Parallelize(
		std::vector<std::shared_ptr<State>> states,
		std::shared_ptr<State>& out)
	{
		auto parallel = std::make_shared<St_Parallel>();

		std::vector<State*> raw;
		raw.reserve(states.size());

		for (const auto& state : states)
			raw.push_back(state.get());

		parallel->SubStates = std::move(states);

		out = std::move(parallel);

		return raw;
	};

	std::vector<State*> StateMachineBase::QueueParallel(std::vector<std::shared_ptr<State>> states)
	{
		std::shared_ptr<State> parallel;

		std::vector<State*> raw = Parallelize(std::move(states), parallel);

		_stateQueue.emplace_back(std::move(parallel));

		return raw;
	};

	std::vector<State*> StateMachineBase::QueueParallelFirst(std::vector<std::shared_ptr<State>> states)
	{
		std::shared_ptr<State> parallel;

		std::vector<State*> raw = Parallelize(std::move(states), parallel);

		_stateQueue.insert(_stateQueue.begin(), std::move(parallel));

		return raw;
	};

	std::vector<State*> StateMachineBase::EnterParallel(std::vector<std::shared_ptr<State>> states)
	{
		std::vector<State*> raw = QueueParallel(std::move(states));

		Proceed();

		return raw;
	};

	State* StateMachineBase::Proceed()
	{
		// St_Parallel overrides normal Proceed: one sub-state finishing is not the whole
		// parallel state finishing.
		if (St_Parallel* parallel = dynamic_cast<St_Parallel*>(current.get()))
			if (parallel->OnChildProceed())
				return _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		State* nextState = _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		// Held, so the outgoing state survives its own OnExit and the incoming state's
		// OnEnter -- which is handed a pointer to it.
		const std::shared_ptr<State> previousState = current;

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
	};

	void StateMachineBase::Enable()
	{
		current = disabledState;
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

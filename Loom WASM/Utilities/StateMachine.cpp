#include "StateMachine.h"

#include "StateMachineMonitor.h"

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
		// Function-local, so a static initialiser elsewhere cannot run before it.
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

	#pragma region Lifetime

	namespace
	{
		// Function-local, since exported static members cannot be defined by a script module.
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

	#pragma region Primitives

	/// Waits for every sub-state to Proceed; each runs its own OnExit as it does.
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

		/// Steps each sub-state on a copy, tracking which runs so its Proceed is attributed.
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
		void OnFixedUpdate() override { Each([](StateBase& s) { s.OnFixedUpdate(); }); };
		void OnLateUpdate() override { Each([](StateBase& s) { s.OnLateUpdate(); }); };
		void OnGui() override { Each([](StateBase& s) { s.OnGui(); }); };

		// Nothing proceeds out of a parallel already leaving.
		void OnExit(StateBase* nextState) override
		{
			for (const auto& state : SubStates)
				if (state)
					state->OnExit(nextState);
		};
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

	#pragma endregion

	#pragma region Control

	void StateMachineBase::Adopt(const std::shared_ptr<StateBase>& state)
	{
		if (!state)
			return;

		state->gameObject = GetGameObject();
		state->stateMachine = this;
	};

	StateBase* StateMachineBase::Proceed()
	{
		// One sub-state finishing does not finish the parallel.
		if (St_Parallel* parallel = dynamic_cast<St_Parallel*>(current.get()))
			if (parallel->OnChildProceed())
				return _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		StateBase* nextState = _stateQueue.empty() ? nullptr : _stateQueue.front().get();

		// Held, so the outgoing state survives its OnExit and the next OnEnter.
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

			// Erased before OnEnter, so a state queued at the front from it survives.
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

	void StateMachineBase::StepFixed()
	{
		if (current)
			current->OnFixedUpdate();
	};

	void StateMachineBase::StepAttached()
	{
		// One snapshot per frame, since destruction only happens in queued tasks.
		for (StateMachineBase* machine : All())
			if (machine->GetGameObject())
				machine->Step();
	};

	void StateMachineBase::StepAttachedFixed()
	{
		for (StateMachineBase* machine : All())
			if (machine->GetGameObject())
				machine->StepFixed();
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

	void StateMachineBase::DrawMachineGui()
	{
		StateMachineMonitor::DrawInline(*this);

		PumpGui();
	};

	#pragma endregion

	#pragma region Inspection

	const std::vector<std::shared_ptr<StateBase>>* StateMachineBase::SubStatesOf(const StateBase* state)
	{
		const St_Parallel* parallel = dynamic_cast<const St_Parallel*>(state);

		return parallel ? &parallel->SubStates : nullptr;
	};

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

#pragma once

#include <mutex>
#include <queue>
#include <stack>


namespace Loom
{
	struct State
	{
		virtual void OnEnter()=0;
		virtual void OnUpdate()=0;
		virtual void OnExit()=0;

		template<typename _State>
		static _State const* Set(auto&&... args)
		{
			static_assert(
				std::is_base_of<State, _State>::value,
				"State must inherit from Loom::State");

			std::scoped_lock lock(mutex);

			if (!stateStack.empty())
				stateStack.top()->OnExit();

			stateStack.push(new _State(std::forward<decltype(args)>(args)...));

			stateStack.top()->OnEnter();

			return (const _State*)stateStack.top();
		};

		template<typename _State>
		static _State const* Queue(auto&&... args)
		{
			static_assert(
				std::is_base_of<State, _State>::value,
				"State must inherit from Loom::State");

			std::scoped_lock lock(mutex);

			State* state = new _State(std::forward<decltype(args)>(args)...);

			stateQueue.push(state);

			return (const _State*)stateQueue.front();
		};

		static State const* Proceed();

		static State const* Current();

	protected:
		friend struct Engine;
		friend struct State;

		static void Update();

	private:
		static inline std::recursive_mutex mutex{ };
		static inline std::stack<State*> stateStack{ };
		static inline std::queue<State*> stateQueue{ };
	};
};

#include "State.h"

namespace Loom
{
	State const* State::Proceed()
	{
		std::scoped_lock lock(mutex);

		if (!stateStack.empty())
			stateStack.top()->OnExit();

		stateQueue.front()->OnEnter();
		
		stateStack.push(stateQueue.front());

		stateQueue.pop();

		return stateStack.top();
	}; 

	State const* State::Current()
	{
		std::scoped_lock lock(mutex);

		return stateStack.top();
	};

	void State::Update()
	{
		if (!stateStack.empty())
			stateStack.top()->OnUpdate();
	};
};

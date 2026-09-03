#include "Loom.h"

#include <iostream>

using namespace Loom;


struct TestState : State
{
	TestState(int&& i)
		: i(std::move(i))
	{ };

	void OnEnter(State* lastState) override
	{ };

	void OnUpdate() override
	{ };

	void OnExit(State* nextState) override
	{ };

private:
	int i = 0;
};


int main()
{
	Engine engine;
	Scene scene{ "Test" };

	// The always-on machine, which is what the old global State stack was: nothing to
	// place, nothing to wire, and the engine pumps it every frame.
	const TestState* state = StaticStateMachine::Instance().Push<TestState>(5);

	StaticStateMachine::Instance().Proceed();

	engine.Start();

	return 0;
};

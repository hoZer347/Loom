#include "Loom.h"

#include <iostream>

using namespace Loom;


struct TestState : State
{
	TestState(int&& i)
		: i(std::move(i))
	{ };

	void OnEnter() override
	{ };

	void OnUpdate() override
	{
		std::cout << "TestState Update: " << i << std::endl;
	};

	void OnExit() override
	{ };

private:
	int i = 0;
};


int main()
{
	Engine engine;
	Scene scene{ "Test" };

	auto* state = State::Queue<TestState>(5);

	State::Proceed();

	engine.Start();

	return 0;
};

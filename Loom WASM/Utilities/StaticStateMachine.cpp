#include "StaticStateMachine.h"


namespace Loom
{
	static StaticStateMachine* s_instance = nullptr;

	StaticStateMachine& StaticStateMachine::Instance()
	{
		if (s_instance == nullptr)
		{
			s_instance = new StaticStateMachine();

			if (OnBoot)
				OnBoot();
		};

		return *s_instance;
	};

	bool StaticStateMachine::Exists()
	{
		return s_instance != nullptr;
	};

	void StaticStateMachine::Destroy()
	{
		delete s_instance;

		s_instance = nullptr;
	};

	void StaticStateMachine::Pump()
	{
		// Never built just to be pumped. A game that does not use it pays nothing, and a
		// machine created mid-frame starts on the next one rather than half-way through
		// this one.
		if (s_instance == nullptr)
			return;

		s_instance->Step();
	};
};

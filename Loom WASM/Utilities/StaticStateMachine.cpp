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
		// Never built just to pump, so unused it costs nothing and a new one starts next frame.
		if (s_instance == nullptr)
			return;

		s_instance->Step();
	};

	void StaticStateMachine::PumpFixed()
	{
		if (s_instance == nullptr)
			return;

		s_instance->StepFixed();
	};
};

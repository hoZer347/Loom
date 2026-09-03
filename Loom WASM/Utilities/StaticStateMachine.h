#pragma once

#include "StateMachine.h"

#include <functional>


namespace Loom
{
	/// Always-on, scene-independent state machine. There is nothing to place and nothing
	/// to wire -- call StaticStateMachine::Instance().SetState&lt;Foo&gt;() from anywhere.
	///
	/// It owns no object and is placed in no scene; the engine pumps it directly from
	/// the frame. One machine, running before anything else is, outliving every scene.
	///
	/// It IS a StateMachineBase and adds nothing to it. There is deliberately no static
	/// mirror of the machine API, so changing that API never touches this file.
	struct StaticStateMachine final : StateMachineBase
	{
		/// The live machine, and the whole public face of this class. Created on demand,
		/// so touching it before the first frame still works.
		static StaticStateMachine& Instance();

		/// True once the machine exists. Lets a shutdown path avoid building one just to
		/// ask whether there was one.
		static bool Exists();

		/// Optional hook invoked once, immediately after the machine is created. Assign
		/// the game's first SetState here when code should pick it -- startState is
		/// applied afterwards, on the first pump, so leave that unset if you seed here.
		static inline std::function<void()> OnBoot{ };

		/// Tears the machine down. The engine has no domain reload to do it, so a test
		/// that wants a clean machine asks for one.
		static void Destroy();

		std::string MachineName() const override { return "StaticStateMachine"; };

	private:
		friend struct Utilities;

		StaticStateMachine() = default;

		/// Driven once per frame by Utilities::Tick.
		static void Pump();
	};
};

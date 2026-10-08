#pragma once

#include "State.h"

#include <functional>


namespace Loom
{
	/// A state assembled from callbacks rather than subclassed -- for the one-off step
	/// that does not deserve its own type.
	///
	/// Every hook is optional and checked before it is called, so an unset one is
	/// skipped rather than throwing.
	struct CustomState final : StateBase
	{
		std::function<void(StateBase* lastState, CustomState& self)> onEnter{ };
		std::function<void(CustomState& self)> onUpdate{ };
		std::function<void(CustomState& self)> onPhysics{ };
		std::function<void(CustomState& self)> onLateUpdate{ };
		std::function<void(CustomState& self)> onGui{ };
		std::function<void(StateBase* nextState, CustomState& self)> onExit{ };

		std::string Name() const override { return name.empty() ? "CustomState" : name; };

		/// What the monitor and the history call it. Worth setting when more than one is
		/// in play, or they are indistinguishable in the log.
		std::string name{ };

		void OnEnter(StateBase* lastState) override { if (onEnter) onEnter(lastState, *this); };
		void OnUpdate() override { if (onUpdate) onUpdate(*this); };
		void OnPhysics() override { if (onPhysics) onPhysics(*this); };
		void OnLateUpdate() override { if (onLateUpdate) onLateUpdate(*this); };
		void OnGui() override { if (onGui) onGui(*this); };
		void OnExit(StateBase* nextState) override { if (onExit) onExit(nextState, *this); };

		/// Proceed, from inside a callback -- StateBase::Proceed is protected, and a lambda is
		/// not a member.
		void Continue() { Proceed(); };
	};
};

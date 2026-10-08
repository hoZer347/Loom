#pragma once

#include "Loom API.h"

#include "ComponentRegistry.h"
#include "State.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	/// A reference to a state that has not been created yet, and a fresh copy of it on
	/// demand.
	///
	/// The reference is a function that builds the state and configures it, rather than a
	/// configured instance to be copied, so each call yields a fresh state and there is no
	/// shared template to mutate by accident.
	///
	///		machine->startState = StateReference::Of&lt;St_Idle&gt;();
	///
	///		machine->startState = StateReference::Of&lt;St_Walk&gt;(
	///			[](St_Walk& state) { state.speed = 4.0f; });
	///
	/// A Serial&lt;StateReference&gt; is written to a scene by its name alone, so only a
	/// registered state comes back from a file, and without its setup.
	struct LOOM_API StateReference final
	{
		StateReference() = default;

		/// Builds a fresh state of the given type, running an optional setup over it.
		template <typename _State, typename _Configure = std::nullptr_t>
		static StateReference Of(_Configure configure = nullptr)
		{
			static_assert(std::is_base_of_v<State, _State>, "State must derive from State");

			StateReference reference;

			// Unqualified, the way HOZER_REGISTER_STATE names it, so a reference
			// written to a file is read back as the same state.
			reference._type_name = PrettyTypeName(typeid(_State).name());
			reference._create =
				[configure]() -> std::shared_ptr<State>
				{
					auto state = std::make_shared<_State>();

					if constexpr (!std::is_same_v<_Configure, std::nullptr_t>)
						if (configure)
							configure(*state);

					return state;
				};

			return reference;
		};

		/// Builds whatever is registered under that name when it is asked to. The name
		/// is kept even while nothing is registered under it, so a scene read before
		/// the scripts that register it are loaded does not lose it.
		static StateReference Named(const std::string& name);

		/// The state's name, for a readout and for the scene file. Empty when nothing
		/// is set.
		const std::string& StateType() const { return _type_name; };

		/// True when this reference will build something.
		bool IsSet() const;

		explicit operator bool() const { return IsSet(); };

		/// A fresh state, or null when nothing is set. A copy every time, so the same
		/// reference can start a machine more than once.
		std::shared_ptr<State> Create() const;

	private:
		std::string _type_name{ };
		std::function<std::shared_ptr<State>()> _create{ };
	};

	/// Names that can be built by string. A state registers itself once, and anything
	/// data-driven (a monitor's "set state" box, a config file, a script) can then
	/// reach it.
	struct LOOM_API StateRegistry final
	{
		static void Register(const std::string& name, std::function<std::shared_ptr<State>()> create);

		/// Takes a name back out: a script library is unloaded before it is rebuilt,
		/// and a factory pointing into code that is no longer mapped is a crash.
		static void Unregister(const std::string& name);

		static std::shared_ptr<State> Create(const std::string& name);

		static bool Contains(const std::string& name);

		/// Every registered name, sorted, for a dropdown to list.
		static std::vector<std::string> Names();

		StateRegistry() = delete;
	};

	namespace detail
	{
		struct StateRegistrar final
		{
			StateRegistrar(const std::string& name, std::function<std::shared_ptr<State>()> create)
			{
				StateRegistry::Register(name, std::move(create));
			};
		};
	};
};


/// Makes a state buildable by name. Written once, at file scope, beneath the state:
///
///		HOZER_REGISTER_STATE(St_Idle);
#define HOZER_REGISTER_STATE(_State)														\
	static const Loom::detail::StateRegistrar _hozer_registrar_##_State(				\
		#_State,																			\
		[]() -> std::shared_ptr<Loom::State> { return std::make_shared<_State>(); })

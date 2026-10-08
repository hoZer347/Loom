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
	///		m_start = StateReference::Of&lt;St_Idle&gt;();
	///
	///		m_start = StateReference::Of&lt;St_Walk&gt;(
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
			static_assert(std::is_base_of_v<StateBase, _State>, "State must derive from StateBase");

			StateReference reference;

			// Unqualified, the way State<T> registers it, so a reference written
			// to a file is read back as the same state.
			reference._type_name = PrettyTypeName(typeid(_State).name());
			reference._create =
				[configure]() -> std::shared_ptr<StateBase>
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
		std::shared_ptr<StateBase> Create() const;

	private:
		std::string _type_name{ };
		std::function<std::shared_ptr<StateBase>()> _create{ };
	};
};

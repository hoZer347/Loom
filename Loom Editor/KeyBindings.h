#pragma once

#include "InputActions.h"

#include <cstddef>
#include <optional>
#include <string>


namespace Loom
{
	/**
	* Loom::KeyBindings
	* - The Key Bindings panel: the project's InputActions, each a name and two
	*   keys picked from a list, added, renamed and taken out here
	* - A name is taken when its box is let go of, and only when
	*   InputActions::Tidy keeps it and no other action has it
	*/
	struct KeyBindings final
	{
		// Whether the actions changed, for the editor to save them. Without a
		// project open there is nowhere to save them, so none are shown.
		bool Draw(bool* open, bool project_open);

	private:
		bool DrawActions();

		// Whether key was set to another.
		static bool DrawKey(const char* label, Key& key);

		// "Action", or the first "Action 2", "Action 3" and on no action has.
		static std::string FreeName();

		// The action whose name is being typed, and what it reads so far.
		std::optional<size_t> m_renaming{ };
		std::string m_name{ };
	};
};

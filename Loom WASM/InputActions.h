#pragma once

#include "Loom API.h"

#include "Input.h"

#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::InputActions
	* - What the player does, by name, and the keys that do it: a key and an
	*   alternate for each, either one doing it. Scripts ask after an action
	*   rather than a key, so its keys can be set again without touching them
	* - A project keeps them beside its .loomproject in a .loominput file of
	*   the same name, a name=key,alt line each, keys by NameOf
	* - The editor's Key Bindings panel sets them, the editor loads the file
	*   with the project, and the web player when it starts
	*/
	struct LOOM_API InputActions final
	{
		static constexpr const char* extension = ".loominput";

		struct Action
		{
			std::string name;
			Key key = Key::None;
			Key alt = Key::None;

			bool operator==(const Action&) const = default;
		};

		// Whether a key for the action went down this frame, is held, or came
		// up. An action by no such name is never done.
		static bool Down(std::string_view name);
		static bool Held(std::string_view name);
		static bool Up(std::string_view name);

		static const std::vector<Action>& All();
		static void Set(std::vector<Action> actions);

		// The action by that name, or null; good until the next Set or Load.
		static const Action* Find(std::string_view name);

		// A name as an action can take it: trimmed, or empty when that leaves
		// nothing or it holds the '=' a file ends a name at.
		static std::string Tidy(std::string_view name);

		// The actions in the file, or none when it is not there.
		static void Load(const std::string& file);
		static bool Save(const std::string& file);

		// Lines with no name Tidy keeps before an '=' are skipped, and a second
		// action by a name already read.
		static std::vector<Action> Read(std::istream& in);
		static void Write(std::ostream& out, const std::vector<Action>& actions);

		InputActions() = delete;
	};
};

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
	* - What the player does, by name, and the input that does it: a key, or
	*   keys pressed together. Scripts ask after an action rather than a key,
	*   so its input can be set again without touching them
	* - Actions may share a name, each another way to do the same thing, and a
	*   name is done by any of them
	* - A project keeps them beside its .loomproject in a .loominput file of
	*   the same name, a name=Key+Key line each, keys by NameOf. Those the Key
	*   Bindings panel has filed in a folder follow a [folder/path] line, and
	*   a folder with nothing in it is a line of its own
	* - The editor's Key Bindings panel sets them, the editor loads the file
	*   with the project, and the web player when it starts
	*/
	struct LOOM_API InputActions final
	{
		InputActions() = delete;

		#pragma region Actions

		struct Action
		{
			std::string name;

			// Held together, the last of them going down does it. None in it
			// is a key not chosen yet, and one with no other key is never done.
			std::vector<Key> keys;

			// Where the Key Bindings panel files it, by path, or empty for the
			// top. Nothing that plays the game looks at it.
			std::string folder;

			bool operator==(const Action&) const = default;
		};

		// Whether an action by that name had its keys go down together this
		// frame, has them all held, or had them let go. A name no action has is
		// never done.
		static bool Down(std::string_view name);
		static bool Held(std::string_view name);
		static bool Up(std::string_view name);

		static const std::vector<Action>& All();
		static const std::vector<std::string>& Folders();
		static void Set(std::vector<Action> actions, std::vector<std::string> folders = { });

		// A name as an action can take it: trimmed, or empty when that leaves
		// nothing or it holds the '=' a file ends a name at.
		static std::string Tidy(std::string_view name);

		#pragma endregion

		#pragma region Files

		static constexpr const char* extension = ".loominput";

		// What a file holds: the actions, and every folder they are filed in,
		// those holding none included.
		struct File
		{
			std::vector<Action> actions;
			std::vector<std::string> folders;

			bool operator==(const File&) const = default;
		};

		// The actions in the file, or none when it is not there.
		static void Load(const std::string& file);
		static bool Save(const std::string& file);

		// Lines with no name Tidy keeps before an '=' are skipped. A line of an
		// older file, a key and an alternate, reads as an action for each.
		static File Read(std::istream& in);
		static void Write(std::ostream& out, const File& file);

		#pragma endregion
	};
};

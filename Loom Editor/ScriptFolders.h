#pragma once

#include "VirtualFolders.h"

#include <string>


namespace Loom
{
	/**
	* Loom::ScriptFolders
	* - The Scripts tab's folders, its script types and shaders filed in them
	*   apart from the files they are written in, so filing one moves no file
	*   and one header's types can sit in different folders
	* - A project keeps them beside its .loomproject in a .loomscripts file of
	*   the same name, as folder=<path> and script.<item>=<path> lines, a
	*   shader's item being its path from the project
	*/
	struct ScriptFolders final : VirtualFolders
	{
		static constexpr const char* extension = ".loomscripts";
		static constexpr const char* script_prefix = "script.";

		// Empty when the file is not there yet.
		void Load(const std::string& file) { VirtualFolders::Load(file, script_prefix); };
		bool Save(const std::string& file) const { return VirtualFolders::Save(file, script_prefix); };
	};
};

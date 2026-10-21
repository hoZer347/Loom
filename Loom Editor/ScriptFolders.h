#pragma once

#include "VirtualFolders.h"

#include <string>


namespace Loom
{
	/**
	* Loom::ScriptFolders
	* - The Scripts panel's folders, its script types filed in them apart from
	*   the headers they are written in, so filing one moves no file and one
	*   header's types can sit in different folders
	* - A project keeps them beside its .loomproject in a .loomscripts file of
	*   the same name, as folder=<path> and script.<type>=<path> lines
	*/
	struct ScriptFolders final : VirtualFolders
	{
		static constexpr const char* extension = ".loomscripts";
		static constexpr const char* folder_key = "folder=";
		static constexpr const char* script_prefix = "script.";

		// Empty when the file is not there yet.
		void Load(const std::string& file);
		bool Save(const std::string& file) const;
	};
};

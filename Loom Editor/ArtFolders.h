#pragma once

#include "VirtualFolders.h"

#include <string>


namespace Loom
{
	/**
	* Loom::ArtFolders
	* - The Art panel's folders, its textures filed in them by their paths in
	*   the project apart from the folders they are in, so filing one moves no
	*   file
	* - A project keeps them beside its .loomproject in a .loomart file of the
	*   same name, as folder=<path> and art.<file>=<path> lines
	*/
	struct ArtFolders final : VirtualFolders
	{
		static constexpr const char* extension = ".loomart";
		static constexpr const char* art_prefix = "art.";

		// Empty when the file is not there yet.
		void Load(const std::string& file) { VirtualFolders::Load(file, art_prefix); };
		bool Save(const std::string& file) const { return VirtualFolders::Save(file, art_prefix); };
	};
};

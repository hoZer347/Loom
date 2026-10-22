#pragma once

#include "VirtualFolders.h"

#include <string>


namespace Loom
{
	/**
	* Loom::TwoDFolders
	* - The 2D panel's folders, its textures filed in them by their paths in
	*   the project apart from the folders they are in, so filing one moves no
	*   file
	* - A project keeps them beside its .loomproject in a .loom2d file of the
	*   same name, as folder=<path> and 2d.<file>=<path> lines
	*/
	struct TwoDFolders final : VirtualFolders
	{
		static constexpr const char* extension = ".loom2d";
		static constexpr const char* two_d_prefix = "2d.";

		// Empty when the file is not there yet.
		void Load(const std::string& file) { VirtualFolders::Load(file, two_d_prefix); };
		bool Save(const std::string& file) const { return VirtualFolders::Save(file, two_d_prefix); };
	};
};

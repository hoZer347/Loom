#pragma once

#include <filesystem>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::EditorLayouts
	* - Named copies of ImGui's settings, one file each in a folder: where every
	*   panel is docked, and whatever else a settings handler adds to them, such
	*   as which panels are open
	* - Loading replaces the docking of the windows that exist, so it belongs
	*   before the dockspace is submitted for the frame
	*/
	namespace EditorLayouts
	{
		constexpr const char* Extension = ".ini";
		constexpr size_t MaxNameLength = 64;

		// Letters, digits, spaces, _ and -, with no space at either end, so a
		// name is always a file name.
		bool IsValidName(const std::string& name);

		// The layouts saved in the folder, sorted.
		std::vector<std::string> Names(const std::filesystem::path& folder);

		// Each false when the file could not be written, read or removed.
		bool Save(const std::filesystem::path& folder, const std::string& name);
		bool Load(const std::filesystem::path& folder, const std::string& name);
		bool Delete(const std::filesystem::path& folder, const std::string& name);
	};
};

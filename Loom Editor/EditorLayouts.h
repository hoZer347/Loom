#pragma once

#include "imgui.h"

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
		#pragma region Names

		constexpr const char* Extension = ".ini";
		constexpr size_t MaxNameLength = 64;

		// Letters, digits, spaces, _ and -, with no space at either end and
		// none of Windows' device names, so a name is always a file name.
		bool IsValidName(const std::string& name);

		// Whether two names are the same file.
		bool SameName(const std::string& a, const std::string& b);

		// The layouts saved in the folder, sorted.
		std::vector<std::string> Names(const std::filesystem::path& folder);

		#pragma endregion

		#pragma region Files

		// Each false when the file could not be written, read or removed. Load
		// gives the IDs of the windows the layout has a place for.
		bool Save(const std::filesystem::path& folder, const std::string& name);
		bool Load(const std::filesystem::path& folder, const std::string& name, std::vector<ImGuiID>* windows = nullptr);
		bool Delete(const std::filesystem::path& folder, const std::string& name);

		#pragma endregion
	};
};

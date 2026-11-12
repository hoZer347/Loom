#pragma once

#include <filesystem>
#include <string>


namespace Loom
{
	/**
	* Loom::CorrinCode
	* - Finds and starts Corrin Code, the separate app that works on a project
	*   from outside the editor
	*/
	struct CorrinCode final
	{
		// The override when it names a file, else the install folder under local_app_data
		// when it holds the exe. Empty when neither does.
		static std::filesystem::path Find(const std::string& override_path, const std::filesystem::path& local_app_data);

		// Find, given the CORRIN_CODE_EXE and LOCALAPPDATA environment variables.
		static std::filesystem::path Locate();

		// Starts the exe with the project folder as its argument. False when it could not start.
		static bool Open(const std::filesystem::path& exe, const std::filesystem::path& project_folder);
	};
};

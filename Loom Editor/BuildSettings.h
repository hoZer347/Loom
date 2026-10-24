#pragma once

#include "ScriptRun.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::BuildSettings
	* - How the project is built into a game of its own: the configuration,
	*   the scene it starts on and the folder it goes in, away from the
	*   project's own Build folder that the editor builds its scripts into
	* - Kept as key=value lines beside the project file, and changed in the
	*   Build Settings window, whose buttons hand build-game.ps1 its command
	*   line
	*/
	struct BuildSettings final
	{
		static constexpr const char* extension = ".loombuild";

		static constexpr std::array<std::string_view, 3> configurations = { "Debug", "Release", "Final" };

		static bool IsConfiguration(std::string_view configuration)
		{
			return std::find(configurations.begin(), configurations.end(), configuration) != configurations.end();
		};

		// What a press in the window asks of the editor.
		enum class Action { None, Changed, Browse, Build, BuildAndRun };

		std::string configuration = std::string(configurations[1]);

		// Relative to the project folder, or absolute.
		std::string scene;
		std::string folder;

		// A setting the file lacks keeps the value it had.
		void Load(const std::string& path);
		bool Save(const std::string& path) const;

		// folder, or for a project that names none, one beside the project
		// folder named after the game.
		std::filesystem::path Output(const std::string& project, const std::string& name) const;

		// What build-game.ps1 is handed, Run asking it to start the game once
		// it is built.
		std::string Arguments(const std::string& project, const std::string& name, bool run) const;

		// The window, which lists the project's scenes each time its list opens,
		// and holds back another build while one is under way.
		Action Draw(bool* open, const std::string& project, const std::string& name,
			const std::function<std::vector<std::string>()>& list_scenes, std::string_view status, bool building);

	private:
		static constexpr size_t folder_capacity = 512;

		static std::filesystem::path DefaultOutput(const std::string& project, const std::string& name);

		std::vector<std::string> m_scenes{ };
		char m_folder[folder_capacity] = { };
	};
};

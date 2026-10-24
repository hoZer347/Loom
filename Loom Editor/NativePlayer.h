#pragma once

#include "ProjectTemplate.h"
#include "ScriptRun.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>


namespace Loom
{
	/**
	* Loom::NativePlayer
	* - Plays a scene as a program of its own: runs play-native.ps1 from the
	*   engine folder, which builds the project's scripts with the engine in
	*   the configuration asked for and runs what it built
	* - What the script says is only ever that the build or the game failed,
	*   and goes to the console as it comes
	*/
	struct NativePlayer final
	{
		static constexpr std::array<std::string_view, 3> configurations = { "Debug", "Release", "Final" };

		static bool IsConfiguration(std::string_view configuration)
		{
			return std::find(configurations.begin(), configurations.end(), configuration) != configurations.end();
		};

		// Where a configuration's build of the project goes, beside the scene
		// it is given.
		static std::filesystem::path Folder(const std::string& project, std::string_view configuration)
		{
			return std::filesystem::path(project) / ProjectTemplate::build_folder / configuration;
		};

		static std::string Arguments(const std::string& project, const std::string& scene, std::string_view configuration)
		{
			return "-Project " + ScriptRun::Quote(project) +
				" -Scene " + ScriptRun::Quote(scene) +
				" -Configuration " + std::string(configuration);
		};

		// Both absolute. Stops whatever an earlier launch left running.
		bool Launch(const std::string& project, const std::string& scene, std::string_view configuration);

	private:
		ScriptRun m_run{ };
	};
};

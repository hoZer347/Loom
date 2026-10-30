#pragma once

#include "EditHistory.h"

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
	*   project's own Build folder that the editor builds its scripts into,
	*   and the fixed rate it steps at, which the editor steps at too
	* - Kept as key=value lines beside the project file, and changed in the
	*   Build Settings tab, whose buttons hand build-game.ps1 its command line
	*/
	struct BuildSettings final
	{
		#pragma region Configurations

		static constexpr std::array<std::string_view, 3> configurations = { "Debug", "Release", "Final" };

		static bool IsConfiguration(std::string_view configuration)
		{
			return std::find(configurations.begin(), configurations.end(), configuration) != configurations.end();
		};

		#pragma endregion

		#pragma region Fixed Rate

		static constexpr float default_fixed_rate = 60.0f;
		static constexpr float min_fixed_rate = 1.0f;
		static constexpr float max_fixed_rate = 1000.0f;

		static bool IsFixedRate(float rate) { return rate >= min_fixed_rate && rate <= max_fixed_rate; };

		#pragma endregion

		BuildSettings() { m_history.Reset(Current()); };

		std::string configuration = std::string(configurations[1]);

		// Relative to the project folder, or absolute.
		std::string scene;
		std::string folder;

		// Fixed updates and physics steps a second, in the editor and the game.
		float fixedRate = default_fixed_rate;

		float FixedDeltaTime() const { return 1.0f / fixedRate; };

		#pragma region File

		static constexpr const char* extension = ".loombuild";

		// A setting the file lacks keeps the value it had. Either leaves
		// nothing unsaved, and Load nothing to undo.
		void Load(const std::string& path);
		bool Save(const std::string& path);

		bool Unsaved() const { return m_history.Unsaved(Current()); };

		#pragma endregion

		#pragma region History

		// One step to undo for a change made outside the tab, such as a
		// folder the browser chose.
		void Keep() { m_history.Keep(Current()); };

		// Starts on scene until another is chosen. Not the user's change, so
		// neither a step to undo nor unsaved.
		void DefaultScene(const std::string& scene);

		#pragma endregion

		#pragma region Paths

		// folder, or for a project that names none, one beside the project
		// folder named after the game.
		std::filesystem::path Output(const std::string& project, const std::string& name) const;

		// The game build-game.ps1 puts in Output.
		std::filesystem::path Program(const std::string& project, const std::string& name) const;

		// What build-game.ps1 is handed.
		std::string Arguments(const std::string& project, const std::string& name) const;

		#pragma endregion

		#pragma region Window

		// What a press in the window asks of the editor.
		enum class Action { None, Save, Browse, Build, BuildAndRun };

		// The tab, which lists the project's scenes each time its list opens,
		// holds back another build while one is under way, and with no project
		// open has nothing to build. Ctrl+Z and Ctrl+Y step through its changes.
		Action Draw(
			bool* open,
			const std::string& project,
			const std::string& name,
			const std::function<std::vector<std::string>()>& list_scenes,
			std::string_view status,
			bool building);

		bool IsFocused() const { return m_focused; };

		#pragma endregion

	private:
		#pragma region Paths

		static std::filesystem::path DefaultOutput(const std::string& project, const std::string& name);

		#pragma endregion

		#pragma region History

		// What is saved, undone and redone.
		struct Settings
		{
			std::string configuration;
			std::string scene;
			std::string folder;
			float fixedRate = default_fixed_rate;

			bool operator==(const Settings&) const = default;
		};

		Settings Current() const { return { configuration, scene, folder, fixedRate }; };

		// Ctrl+Z and Ctrl+Y, and whether Ctrl+S was pressed.
		bool Shortcuts();

		#pragma endregion

		static constexpr size_t folder_capacity = 512;

		std::vector<std::string> m_scenes{ };
		char m_folder[folder_capacity] = { };

		EditHistory<Settings> m_history;
		bool m_focused = false;
	};
};

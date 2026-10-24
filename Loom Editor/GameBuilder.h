#pragma once

#include "ScriptRun.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>


namespace Loom
{
	/**
	* Loom::GameBuilder
	* - Builds a project into a game of its own: runs build-game.ps1 from the
	*   engine folder, which compiles the project's scripts with the engine
	*   into the folder asked for and copies the project's files beside it
	* - Asked to, starts the game once it is built, itself rather than through
	*   the script, so the game comes to the front and outlives the editor
	* - What the script says is only ever that the build failed, and goes to
	*   the console as it comes
	*/
	struct GameBuilder final
	{
		enum class Status { Idle, Building, Built, Failed };

		// BuildSettings::Arguments, and BuildSettings::Program to start it once
		// built or nothing. Stops a build an earlier launch left running.
		bool Launch(const std::string& arguments, const std::filesystem::path& run);

		Status GetStatus() const { return m_status; };

		static std::string_view Label(Status status)
		{
			switch (status)
			{
			case Status::Building:	return "Building";
			case Status::Built:		return "Built";
			case Status::Failed:	return "Failed";
			default:				return "";
			};
		};

	private:
		ScriptRun m_run{ };
		std::atomic<Status> m_status = Status::Idle;
	};
};

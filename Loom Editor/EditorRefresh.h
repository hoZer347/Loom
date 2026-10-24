#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::EditorRefresh
	* - Ctrl+Shift+R's rebuild of the editor itself, through the engine's
	*   solution, so whatever has changed in the engine since this executable
	*   was linked is in the one the editor restarts as
	* - A running executable cannot be written over, nor can the symbols
	*   FieldNames holds open, but both can be renamed. So they are moved aside
	*   and copies put back where they were, which MSBuild either finds up to
	*   date or links over
	* - MSBuild runs on a worker thread, as the scripts' does, and its errors
	*   go to the Console panel
	*/
	struct EditorRefresh final
	{
		EditorRefresh() = default;
		~EditorRefresh();

		EditorRefresh(const EditorRefresh&) = delete;
		EditorRefresh& operator=(const EditorRefresh&) = delete;

		// Started and not yet handed over by Poll.
		bool IsRunning() const { return m_current != nullptr; };

		// Kicks off the build. Without the engine's sources or MSBuild there is
		// nothing to build, and the refresh finishes at once with the editor as
		// it is.
		void Start();

		enum struct Progress { Idle, Building, Finished };

		// Called once a frame: reports the moment the build ends. A failed one
		// has put the executable back as it was.
		Progress Poll();

		bool Succeeded() const { return m_succeeded; };

		// The executable this process was started from, which the build may
		// have replaced.
		const std::filesystem::path& GetExecutable() const { return m_executable; };

		// Starts it with those arguments after it.
		static bool Relaunch(const std::filesystem::path& executable, const std::string& arguments);

	private:
		struct Build
		{
			std::atomic<bool> done{ false };
			std::atomic<bool> succeeded{ false };
		};

		std::shared_ptr<Build> m_current{ };
		std::thread m_worker{ };
		bool m_succeeded = false;

		std::filesystem::path m_executable{ };

		// Each file moved aside, and where to.
		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> m_movedAside{ };
	};
};

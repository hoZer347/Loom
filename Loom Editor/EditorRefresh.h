#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>


namespace Loom
{
	/**
	* Loom::EditorRefresh
	* - Ctrl+Shift+R's rebuild of the editor itself, through the engine's
	*   solution, so whatever has changed in the engine since this executable
	*   was linked is in the one the editor restarts as
	* - A running executable cannot be written over, but it can be renamed, so
	*   it is moved aside and a copy is put back where it was. MSBuild either
	*   finds the copy up to date or links over it
	* - MSBuild runs on a worker thread, as the scripts' does, and its output
	*   goes to the Console panel
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

		// Starts the executable this process was started from, which the build
		// may have replaced, with those arguments after it.
		bool Relaunch(const std::string& arguments) const;

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
		std::filesystem::path m_movedAside{ };
	};
};

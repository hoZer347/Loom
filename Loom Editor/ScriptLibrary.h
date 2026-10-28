#pragma once

#include "ComponentRegistry.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::ScriptLibrary
	* - A project's scripts, compiled and loaded
	* - The scripts are an ordinary C++ project that builds a DLL and links the
	*   editor's import library, so there is exactly one engine in the process:
	*   the task queue, the object tables and the component registry all live in
	*   the executable and the scripts reach into them
	* - Building runs MSBuild on a worker thread; loading and unloading are left
	*   to the main thread, because they add and remove the component types the
	*   GUI is walking
	*/
	struct ScriptLibrary final
	{
		#pragma region Lifetime

		ScriptLibrary() = default;
		~ScriptLibrary();

		ScriptLibrary(const ScriptLibrary&) = delete;
		ScriptLibrary& operator=(const ScriptLibrary&) = delete;

		#pragma endregion

		#pragma region Project

		// Both absolute: the .vcxproj to build and the .dll it produces.
		void SetProject(const std::string& project, const std::string& library);
		void Clear();

		bool HasProject() const { return !m_project.empty(); };

		// Whether any script source is newer than the library built from it.
		// Answered from a short-lived cache: the panel asks every frame, and
		// scanning a directory tree at frame rate is pure waste.
		bool IsStale() const;

		const std::string& GetProject() const { return m_project; };
		const std::string& GetLibrary() const { return m_library; };
		const std::string& GetStatus() const { return m_status; };

		#pragma endregion

		#pragma region Building

		bool IsBuilding() const { return m_current && !m_current->done; };

		// No build running, and none finished and waiting on Poll to hand over.
		bool IsIdle() const { return !m_current; };

		// Kicks off a build. The caller unloads first (and puts back whatever it
		// had to tear down) around Poll returning Finished.
		bool Build();

		enum struct Progress { Idle, Building, Finished };

		// Called once a frame: reports the moment a build ends, so the caller can
		// load the result on the main thread.
		Progress Poll();

		// How far the build under way has got, going by how long the last of
		// its kind took. None until one of its kind has finished.
		std::optional<float> BuildProgress() const;

		bool LastBuildSucceeded() const { return m_succeeded; };

		#pragma endregion

		#pragma region Loading

		bool Load();
		void Unload();

		// The component types the loaded library registered.
		const std::vector<std::string>& GetTypes() const { return m_types; };
		const std::vector<std::string>& GetStates() const { return m_states; };

		#pragma endregion

		#pragma region Tools

		// devenv.exe from the newest Visual Studio on this machine, or empty
		// when there is none. Looked up once and held.
		static const std::string& FindVisualStudio();

		// The MSBuild this machine has. Empty when none is found.
		static std::string FindMSBuild();

		// The configuration this editor was built in, which is the one anything
		// built to run inside it has to be built in too.
		static const char* Configuration();

		#pragma endregion

	private:
		#pragma region Building

		// Lets go of a build that is no longer wanted. MSBuild is still running
		// and there is no hurrying it, so the thread is left to finish into a
		// result nothing reads - waiting here would stop the editor pumping for
		// the rest of the compile.
		void AbandonBuild();

		// Whether the library predates the editor running it. MSBuild has no idea
		// the engine moved, so this is also what decides a full rebuild.
		bool BuiltAgainstOlderEditor() const;

		// Where how long the library's builds took is kept between sessions.
		std::filesystem::path TimingsFile() const;

		#pragma endregion

		#pragma region Loading

		// Clears out the copies of a library that earlier editors loaded. The
		// one a running editor still holds cannot be removed, and is left.
		static void SweepLoadedCopies(const std::string& library);

		// Takes the library's component and state types back out of the
		// registries.
		void RestoreRegistry();

		#pragma endregion

		#pragma region Project

		std::string m_project{ };
		std::string m_library{ };
		std::string m_status{ "No script project." };

		mutable std::chrono::steady_clock::time_point m_staleCheckedAt{ };
		mutable bool m_stale = false;

		#pragma endregion

		#pragma region Building

		bool m_succeeded = false;

		// When the build under way started, whether it is a rebuild, how long
		// the last of its kind took, and when the library was last written
		// before it.
		std::chrono::steady_clock::time_point m_buildStarted{ };
		bool m_rebuilding = false;
		std::optional<double> m_expected{ };
		std::filesystem::file_time_type m_libraryBefore{ };

		// One per build, held by the worker as well, so a build nobody is waiting
		// for any more has somewhere to put its result that nothing reads.
		struct Compilation
		{
			std::atomic<bool> done{ false };
			std::atomic<bool> succeeded{ false };
		};

		std::shared_ptr<Compilation> m_current{ };

		std::thread m_worker{ };

		// Builds nobody is waiting for any more. Their results go nowhere, but
		// they are still writing MSBuild's output to the console, so the
		// destructor waits for them rather than letting them outlive the log
		// they are writing to.
		std::vector<std::thread> m_abandoned{ };

		#pragma endregion

		#pragma region Loading

		// The DLL is copied aside before loading, so a rebuild is not blocked by
		// the editor holding the file it is trying to write. The copy is named
		// after this process, so two editors on one project do not fight over it.
		std::string m_loadedCopy{ };

		void* m_module = nullptr;
		std::vector<std::string> m_types{ };

		// The state names this library registered.
		std::vector<std::string> m_states{ };

		// How many test cases were registered before this library's.
		size_t m_testsBefore = 0;

		// The whole registry as it stood before this library registered anything.
		// Kept in full rather than just the entries it went on to displace, because
		// std::functions cannot be compared: unloading puts all of it back, which
		// restores anything the library registered over the top of and is a no-op
		// for the rest. Each with the category it was filed under.
		std::map<std::string, std::pair<ComponentRegistry::Factory, std::string>> m_before{ };

		#pragma endregion
	};
};

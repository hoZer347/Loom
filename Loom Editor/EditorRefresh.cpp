#include "EditorRefresh.h"

#include "Executable.h"
#include "HiddenProcess.h"
#include "ProjectTemplate.h"
#include "ScriptLibrary.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace Loom
{
	namespace
	{
#ifdef _WIN32
		constexpr const char* solution = "Loom.sln";
		constexpr const char* builder_name = "MSBuild";

		// <file>.<pid>.old, so two editors run from one build each move their own.
		constexpr const char* moved_aside_extension = ".old";

		constexpr const char* symbols_extension = ".pdb";

		// What an editor still has open is locked, and left.
		void SweepMovedAside(const std::filesystem::path& file)
		{
			const std::string prefix = file.filename().string() + '.';

			std::error_code code;

			for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), code))
				if (entry.path().filename().string().starts_with(prefix) &&
					entry.path().extension() == moved_aside_extension)
					std::filesystem::remove(entry.path(), code);
		};
#else
		// Where CMakeLists.txt says to configure it, and the build an editor built there has.
		constexpr const char* solution = "build/CMakeCache.txt";
		constexpr const char* builder_name = "CMake";
#endif
	};

	#pragma region Lifetime

	EditorRefresh::~EditorRefresh()
	{
		if (m_worker.joinable())
			m_worker.join();

		// A build that failed puts the executable back.
		Poll();
	};

	#pragma endregion

	#pragma region Build

	void EditorRefresh::Start()
	{
		if (IsRunning())
			return;

		m_current = std::make_shared<Build>();
		m_succeeded = false;
		m_executable = ExecutablePath();
		m_movedAside.clear();

		const auto finish = [this](bool succeeded)
		{
			m_current->succeeded = succeeded;
			m_current->done = true;
		};

		const std::filesystem::path sources = std::filesystem::path(ProjectTemplate::EngineRoot()) / solution;
		const std::string msbuild = ScriptLibrary::FindMSBuild();

		std::error_code code;

		if (m_executable.empty())
		{
			finish(false);
			return;
		};

		if (!std::filesystem::exists(sources, code) || msbuild.empty())
		{
			std::cout
				<< "Full refresh: "
				<< (msbuild.empty() ? std::string("no ") + builder_name + " to build the editor with" : "no engine sources beside this editor")
				<< ", so it restarts as it is." << std::endl;

			finish(true);
			return;
		};

		// Elsewhere the linker writes a new file in place of a running one, so nothing is moved aside.
#ifdef _WIN32
		std::filesystem::path symbols = m_executable;
		symbols.replace_extension(symbols_extension);

		for (const std::filesystem::path& file : { m_executable, symbols })
		{
			if (!std::filesystem::exists(file, code))
				continue;

			SweepMovedAside(file);

			std::filesystem::path aside = file;
			aside += "." + std::to_string(GetCurrentProcessId()) + moved_aside_extension;

			if (!MoveFileExW(file.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				std::cerr << "Full refresh: could not move " << file.string() << " aside to build over it." << std::endl;

				finish(false);
				return;
			};

			m_movedAside.emplace_back(file, aside);

			// Timestamps kept, so a build with nothing to do finds it up to date.
			if (!CopyFileW(aside.c_str(), file.c_str(), FALSE))
			{
				std::cerr << "Full refresh: could not put a copy of " << file.string() << " back to build over." << std::endl;

				finish(false);
				return;
			};
		};

#endif

		const std::string command = Command(msbuild, sources, ScriptLibrary::Configuration());

		std::cout << "Full refresh: building the editor (" << ScriptLibrary::Configuration() << ")..." << std::endl;

		m_worker = std::thread(
			[command, build = m_current]()
			{
				build->succeeded = HiddenProcess::Run(
					command,
					[](const std::string& line)
					{
						if (!line.empty())
							std::cout << "  " << line << std::endl;
					});
				build->done = true;
			});
	};

	EditorRefresh::Progress EditorRefresh::Poll()
	{
		if (!m_current)
			return Progress::Idle;

		if (!m_current->done)
			return Progress::Building;

		if (m_worker.joinable())
			m_worker.join();

		m_succeeded = m_current->succeeded;
		m_current.reset();

#ifdef _WIN32
		// Renaming them back is allowed, as moving them aside was.
		if (!m_succeeded)
			for (const auto& [file, aside] : m_movedAside)
				if (!MoveFileExW(aside.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
					std::cerr << "Full refresh: could not put " << file.string() << " back" << std::endl;
#endif

		m_movedAside.clear();

		return Progress::Finished;
	};

	#pragma endregion

	#pragma region Relaunch

	void EditorRefresh::NoteExecutable()
	{
		m_executable = ExecutablePath();
	};

	bool EditorRefresh::Relaunch(const std::filesystem::path& executable, const std::string& arguments)
	{
#ifndef _WIN32
		// Before the fork, since the child of a process with threads may not allocate.
		const std::string command = "exec \"" + executable.string() + "\" " + arguments;

		const pid_t child = fork();

		if (child == 0)
		{
			setsid();
			execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
			_exit(1);
		};

		if (child < 0)
			std::cerr << "Full refresh: could not start " << executable.string() << " again" << std::endl;

		return child > 0;
#else
		const int length = MultiByteToWideChar(CP_ACP, 0, arguments.c_str(), -1, nullptr, 0);

		std::wstring wide(length, L'\0');
		MultiByteToWideChar(CP_ACP, 0, arguments.c_str(), -1, wide.data(), length);
		wide.resize(length - 1);

		std::wstring command = L"\"" + executable.wstring() + L"\" " + wide;

		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION process{ };

		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
		{
			std::cerr << "Full refresh: could not start " << executable.string() << " again" << std::endl;
			return false;
		};

		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		return true;
#endif
	};

	#pragma endregion
};

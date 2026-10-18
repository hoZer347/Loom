#include "EditorRefresh.h"

#include "HiddenProcess.h"
#include "ProjectTemplate.h"
#include "ScriptLibrary.h"

#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
		constexpr const char* solution = "Loom.sln";
		constexpr const char* target = "Loom Editor";

		// <executable>.<process id>.old, so two editors run from one build each
		// move their own.
		constexpr const char* moved_aside_extension = ".old";

		std::filesystem::path Executable()
		{
			wchar_t buffer[MAX_PATH]{ };

			const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);

			return length == 0 || length == MAX_PATH
				? std::filesystem::path()
				: std::filesystem::path(buffer);
		};

		// What earlier refreshes moved aside. One an editor is still running
		// from is locked, and left.
		void SweepMovedAside(const std::filesystem::path& executable)
		{
			const std::string prefix = executable.filename().string() + '.';

			std::error_code code;

			for (const auto& entry : std::filesystem::directory_iterator(executable.parent_path(), code))
				if (entry.path().filename().string().starts_with(prefix) &&
					entry.path().extension() == moved_aside_extension)
					std::filesystem::remove(entry.path(), code);
		};
	};

	EditorRefresh::~EditorRefresh()
	{
		if (m_worker.joinable())
			m_worker.join();

		// A build that failed puts the executable back.
		Poll();
	};

	void EditorRefresh::Start()
	{
		if (IsRunning())
			return;

		m_current = std::make_shared<Build>();
		m_succeeded = false;
		m_executable = Executable();
		m_movedAside.clear();

		const auto finish = [this](bool succeeded)
		{
			m_current->succeeded = succeeded;
			m_current->done = true;
		};

		const std::filesystem::path sources = std::filesystem::path(ProjectTemplate::EngineRoot()) / solution;
		const std::string msbuild = ScriptLibrary::FindMSBuild();

		std::error_code code;

		if (!std::filesystem::exists(sources, code) || msbuild.empty())
		{
			std::cout
				<< "Full refresh: "
				<< (msbuild.empty() ? "no MSBuild to build the editor with" : "no engine sources beside this editor")
				<< ", so it restarts as it is." << std::endl;

			finish(true);
			return;
		};

		SweepMovedAside(m_executable);

		std::filesystem::path aside = m_executable;
		aside += "." + std::to_string(GetCurrentProcessId()) + moved_aside_extension;

		if (!MoveFileExW(m_executable.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			std::cerr << "Full refresh: could not move the running editor aside to build over it." << std::endl;

			finish(false);
			return;
		};

		m_movedAside = aside;

		// Kept as it was, timestamps included, so a build with nothing to do
		// finds it up to date.
		if (!CopyFileW(aside.c_str(), m_executable.c_str(), FALSE))
		{
			std::cerr << "Full refresh: could not put a copy of the editor back to build over." << std::endl;

			finish(false);
			return;
		};

		const std::string command =
			'"' + msbuild + "\" \"" + sources.string() + "\" \"-t:" + target + '"' +
			" -p:Configuration=" + ScriptLibrary::Configuration() +
			" -p:Platform=x64 -nologo -v:m -m";

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

		// Renaming the running image back is allowed, as moving it aside was.
		if (!m_succeeded && !m_movedAside.empty() &&
			!MoveFileExW(m_movedAside.c_str(), m_executable.c_str(), MOVEFILE_REPLACE_EXISTING))
			std::cerr << "Full refresh: could not put the editor back at " << m_executable.string() << std::endl;

		m_movedAside.clear();

		return Progress::Finished;
	};

	bool EditorRefresh::Relaunch(const std::string& arguments) const
	{
		const int length = MultiByteToWideChar(CP_ACP, 0, arguments.c_str(), -1, nullptr, 0);

		std::wstring wide(length, L'\0');
		MultiByteToWideChar(CP_ACP, 0, arguments.c_str(), -1, wide.data(), length);
		wide.resize(length - 1);

		std::wstring command = L"\"" + m_executable.wstring() + L"\" " + wide;

		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION process{ };

		if (!CreateProcessW(m_executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
		{
			std::cerr << "Full refresh: could not start " << m_executable.string() << " again" << std::endl;
			return false;
		};

		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		return true;
	};
};

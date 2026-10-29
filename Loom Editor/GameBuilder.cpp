#include "GameBuilder.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>

#include <sys/wait.h>
#include <unistd.h>
#endif


namespace Loom
{
	namespace
	{
		// From its own folder, and in no job of the editor's.
		bool Start(const std::filesystem::path& program)
		{
#ifdef _WIN32
			std::wstring command_line = L"\"" + program.wstring() + L"\"";
			STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
			PROCESS_INFORMATION started{ };

			if (!CreateProcessW(
				program.c_str(),
				command_line.data(),
				nullptr,
				nullptr,
				FALSE,
				0,
				nullptr,
				program.parent_path().c_str(),
				&startup,
				&started))
				return false;

			CloseHandle(started.hThread);
			CloseHandle(started.hProcess);

			return true;
#else
			// Twice, so the game is no child of the editor's left for it to wait on.
			const pid_t child = fork();

			if (child < 0)
				return false;

			if (child == 0)
			{
				setsid();

				if (fork() == 0 && chdir(program.parent_path().c_str()) == 0)
					execl(program.c_str(), program.c_str(), nullptr);

				_exit(0);
			};

			waitpid(child, nullptr, 0);

			return access(program.c_str(), X_OK) == 0;
#endif
		};

		int LastError()
		{
#ifdef _WIN32
			return (int)GetLastError();
#else
			return errno;
#endif
		};
	};

	bool GameBuilder::Launch(const std::string& arguments, const std::filesystem::path& run)
	{
		// Before the start, which stops the last run, so only this one's reader writes it.
		m_status = Status::Building;

		const bool started = m_run.Start("build-game.ps1", arguments,
			[this, run](HiddenProcess hidden, const std::atomic<bool>& stopping)
			{
				hidden.ReadLines(
					[&](const std::string& line)
					{
						if (!stopping && !line.empty())
							std::cerr << "[build] " << line << std::endl;
					});

				const unsigned long code = hidden.Finish();

				if (stopping)
					return;

				if (code == 0 && !run.empty() && !Start(run))
				{
					std::cerr << "Could not start " << run.string() << " (error " << LastError() << ')' << std::endl;
					m_status = Status::Failed;
					return;
				};

				m_status = code == 0 ? Status::Built : Status::Failed;
			});

		if (!started)
			m_status = Status::Failed;

		return started;
	};
};

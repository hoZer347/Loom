#include "GameBuilder.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


namespace Loom
{
#ifdef _WIN32
	namespace
	{
		// From its own folder, and in no job of the editor's.
		bool Start(const std::filesystem::path& program)
		{
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
					std::cerr << "Could not start " << run.string() << " (error " << GetLastError() << ')' << std::endl;
					m_status = Status::Failed;
					return;
				};

				m_status = code == 0 ? Status::Built : Status::Failed;
			});

		if (!started)
			m_status = Status::Failed;

		return started;
	};
#else
	bool GameBuilder::Launch(const std::string&, const std::filesystem::path&)
	{
		m_status = Status::Failed;

		return false;
	};
#endif
};

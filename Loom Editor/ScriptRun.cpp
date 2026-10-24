#include "ScriptRun.h"

#include "ProjectTemplate.h"

#include <filesystem>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	ScriptRun::~ScriptRun()
	{
		Stop();
	};

	bool ScriptRun::Start(const std::string& script, const std::string& arguments, const Reader& read)
	{
		Stop();

		const std::filesystem::path path = std::filesystem::path(ProjectTemplate::EngineRoot()) / script;

		m_job = CreateJobObjectA(nullptr, nullptr);

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

		const std::string command_line =
			"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + Quote(path.string()) + ' ' + arguments;

		HiddenProcess hidden;

		if (!hidden.Start(command_line, m_job))
		{
			std::cerr << "Could not start " << path.string() << " (error " << GetLastError() << ')' << std::endl;
			Stop();
			return false;
		};

		// The job holds everything it starts; nothing waits on the script itself.
		CloseHandle(hidden.process);
		hidden.process = nullptr;

		m_reader = std::thread(read, hidden, std::cref(m_stopping));

		return true;
	};

	void ScriptRun::Stop()
	{
		m_stopping = true;

		// The last handle to the job: closing it kills everything in it, and
		// the reader runs out of pipe.
		if (m_job != nullptr)
		{
			CloseHandle(m_job);
			m_job = nullptr;
		};

		if (m_reader.joinable())
			m_reader.join();

		m_stopping = false;
	};
};

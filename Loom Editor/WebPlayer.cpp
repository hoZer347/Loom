#include "WebPlayer.h"

#include "HiddenProcess.h"
#include "ProjectTemplate.h"

#include <filesystem>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>


namespace Loom
{
	namespace
	{
		const char* const prefix = "[web] ";

		// Quoted for a command line. A trailing backslash would escape the
		// closing quote, and a folder means the same without one.
		std::string Quote(std::string argument)
		{
			while (!argument.empty() && (argument.back() == '\\' || argument.back() == '/'))
				argument.pop_back();

			return '"' + argument + '"';
		};

		void OpenInBrowser(const std::string& url)
		{
			// ShellExecute may hand the work to a shell extension, which wants COM.
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

			const HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

			// The one error code ShellExecute has: anything at or below 32.
			if ((INT_PTR)result <= 32)
				std::cerr << prefix << "Could not open " << url << " (error " << (INT_PTR)result << ')' << std::endl;
			else
				std::cout << prefix << "Opened " << url << std::endl;

			if (SUCCEEDED(com))
				CoUninitialize();
		};

		// Prints what the script says, a line at a time, and opens the page
		// when the server comes up. Returns when everything holding the pipe
		// has exited.
		void Read(HiddenProcess hidden, const std::string& page)
		{
			hidden.ReadLines(
				[&](const std::string& line)
				{
					if (line.empty())
						return;

					std::cout << prefix << line << std::endl;

					if (const std::string url = WebPlayer::PageUrl(line, page); !url.empty())
						OpenInBrowser(url);
				});
		};
	};

	WebPlayer::~WebPlayer()
	{
		Stop();
	};

	bool WebPlayer::Launch(const std::string& project, const std::string& scene)
	{
		Stop();

		const std::filesystem::path script =
			std::filesystem::path(ProjectTemplate::EngineRoot()) / "play-web.ps1";

		// Where play-web.ps1 writes the page: beside the scene, served from the project.
		const std::string page =
			'/' + (std::filesystem::path(scene).parent_path().lexically_relative(project) / "index.html").generic_string();

		m_job = CreateJobObjectA(nullptr, nullptr);

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

		const std::string command_line =
			"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + Quote(script.string()) +
			" -Project " + Quote(project) +
			" -Scene " + Quote(scene);

		HiddenProcess hidden;

		if (!hidden.Start(command_line, m_job))
		{
			std::cerr << prefix << "Could not start " << script.string() << " (error " << GetLastError() << ')' << std::endl;
			Stop();
			return false;
		};

		// The job holds the server; nothing waits on the script itself.
		CloseHandle(hidden.process);
		hidden.process = nullptr;

		std::cout << prefix << "Building " << project << " for the web" << std::endl;

		m_reader = std::thread(Read, hidden, page);

		return true;
	};

	void WebPlayer::Stop()
	{
		// The last handle to the job: closing it kills everything in it, and
		// the reader runs out of pipe.
		if (m_job != nullptr)
		{
			CloseHandle(m_job);
			m_job = nullptr;
		};

		if (m_reader.joinable())
			m_reader.join();
	};
};

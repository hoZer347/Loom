#include "WebPlayer.h"

#include "ProjectTemplate.h"

#include <filesystem>
#include <iostream>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>


namespace Loom
{
	namespace
	{
		const char* const prefix = "[web] ";

		constexpr DWORD read_size = 4096;

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
		void Read(HANDLE pipe, const std::string& page)
		{
			std::string pending;
			char buffer[read_size];
			DWORD read = 0;

			while (ReadFile(pipe, buffer, read_size, &read, nullptr) && read > 0)
			{
				pending.append(buffer, read);

				for (size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n'))
				{
					std::string line = pending.substr(0, end);
					pending.erase(0, end + 1);

					while (!line.empty() && line.back() == '\r')
						line.pop_back();

					if (line.empty())
						continue;

					std::cout << prefix << line << std::endl;

					if (const std::string url = WebPlayer::PageUrl(line, page); !url.empty())
						OpenInBrowser(url);
				};
			};

			CloseHandle(pipe);
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

		SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE read = nullptr;
		HANDLE write = nullptr;

		if (!CreatePipe(&read, &write, &inherit, 0))
		{
			std::cerr << prefix << "Could not create a pipe for the build" << std::endl;
			return false;
		};

		SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

		m_job = CreateJobObjectA(nullptr, nullptr);

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

		std::string command_line =
			"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + Quote(script.string()) +
			" -Project " + Quote(project) +
			" -Scene " + Quote(scene);

		// Only the pipe is handed down. Inheriting everything would pass on
		// whatever other pipe the editor has open at that moment, such as a
		// script build's, and the server never exits to let go of it.
		SIZE_T attributes_size = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributes_size);

		std::vector<char> attributes(attributes_size);

		STARTUPINFOEXA startup{ };
		startup.StartupInfo.cb = sizeof(STARTUPINFOEXA);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdOutput = write;
		startup.StartupInfo.hStdError = write;
		startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());

		InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributes_size);
		UpdateProcThreadAttribute(
			startup.lpAttributeList,
			0,
			PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			&write,
			sizeof(write),
			nullptr,
			nullptr);

		PROCESS_INFORMATION process{ };

		// Suspended until it is in the job, so nothing it starts can get out first.
		const bool started = CreateProcessA(
			nullptr,
			command_line.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
			nullptr,
			nullptr,
			&startup.StartupInfo,
			&process);

		DeleteProcThreadAttributeList(startup.lpAttributeList);

		// The child holds its own copy now; with this one gone, the read end
		// sees the end of the pipe once the child and everything it started exit.
		CloseHandle(write);

		if (!started)
		{
			std::cerr << prefix << "Could not start " << script.string() << " (error " << GetLastError() << ')' << std::endl;
			CloseHandle(read);
			Stop();
			return false;
		};

		AssignProcessToJobObject(m_job, process.hProcess);
		ResumeThread(process.hThread);

		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		std::cout << prefix << "Building " << project << " for the web" << std::endl;

		m_reader = std::thread(Read, read, page);

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

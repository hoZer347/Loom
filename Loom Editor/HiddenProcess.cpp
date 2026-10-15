#include "HiddenProcess.h"

#include <array>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
		constexpr DWORD read_size = 4096;
	};

	bool HiddenProcess::Start(std::string command_line, void* job)
	{
		SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE read = nullptr;
		HANDLE write = nullptr;

		if (!CreatePipe(&read, &write, &inherit, 0))
			return false;

		SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

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

		PROCESS_INFORMATION started{ };

		const bool ok = CreateProcessA(
			nullptr,
			command_line.data(),
			nullptr,
			nullptr,
			TRUE,
			(job != nullptr ? CREATE_SUSPENDED : 0) | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
			nullptr,
			nullptr,
			&startup.StartupInfo,
			&started);

		DeleteProcThreadAttributeList(startup.lpAttributeList);

		// The child holds its own copy now; with this one gone, the read end
		// sees the end of the pipe once the child and everything it started exit.
		CloseHandle(write);

		if (!ok)
		{
			CloseHandle(read);
			return false;
		};

		// Suspended until it is in the job, so nothing it starts can get out first.
		if (job != nullptr)
		{
			AssignProcessToJobObject(job, started.hProcess);
			ResumeThread(started.hThread);
		};

		CloseHandle(started.hThread);

		process = started.hProcess;
		output = read;

		return true;
	};

	void HiddenProcess::ReadLines(const std::function<void(const std::string&)>& on_line)
	{
		std::array<char, read_size> buffer{ };
		std::string line;
		DWORD count = 0;

		while (ReadFile(output, buffer.data(), read_size, &count, nullptr) && count > 0)
			for (DWORD i = 0; i < count; i++)
			{
				const char c = buffer[i];

				if (c == '\r')
					continue;

				if (c != '\n')
				{
					line += c;
					continue;
				};

				on_line(line);
				line.clear();
			};

		if (!line.empty())
			on_line(line);

		CloseHandle(output);
		output = nullptr;
	};

	unsigned long HiddenProcess::Finish()
	{
		DWORD exit_code = 1;

		WaitForSingleObject(process, INFINITE);
		GetExitCodeProcess(process, &exit_code);
		CloseHandle(process);
		process = nullptr;

		return exit_code;
	};

	bool HiddenProcess::Run(const std::string& command_line, const std::function<void(const std::string&)>& on_line)
	{
		HiddenProcess hidden;

		if (!hidden.Start(command_line))
			return false;

		hidden.ReadLines(on_line);

		return hidden.Finish() == 0;
	};
};

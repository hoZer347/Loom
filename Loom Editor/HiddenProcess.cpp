#include "HiddenProcess.h"

#include <array>

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifdef _WIN32

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

		// With this handle gone, the pipe ends once the child and its children exit.
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
};

#elif !defined(__EMSCRIPTEN__)

#include <cstdint>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>


namespace Loom
{
	namespace
	{
		constexpr size_t read_size = 4096;
		constexpr int exec_failed = 127;

		void* FromInt(intptr_t value)
		{
			return reinterpret_cast<void*>(value);
		};

		intptr_t ToInt(void* value)
		{
			return reinterpret_cast<intptr_t>(value);
		};
	};

	// The child leads a process group of its own, so the job's owner can stop everything it started at once.
	bool HiddenProcess::Start(std::string command_line, void*)
	{
		int pipe_ends[2];

		if (pipe2(pipe_ends, O_CLOEXEC) != 0)
			return false;

		const pid_t child = fork();

		if (child < 0)
		{
			close(pipe_ends[0]);
			close(pipe_ends[1]);
			return false;
		};

		if (child == 0)
		{
			setpgid(0, 0);
			dup2(pipe_ends[1], STDOUT_FILENO);
			dup2(pipe_ends[1], STDERR_FILENO);
			execl("/bin/sh", "sh", "-c", command_line.c_str(), nullptr);
			_exit(exec_failed);
		};

		close(pipe_ends[1]);

		process = FromInt(child);
		output = FromInt(pipe_ends[0]);

		return true;
	};

	void HiddenProcess::ReadLines(const std::function<void(const std::string&)>& on_line)
	{
		std::array<char, read_size> buffer{ };
		std::string line;
		ssize_t count = 0;
		const int fd = static_cast<int>(ToInt(output));

		while ((count = read(fd, buffer.data(), read_size)) > 0)
			for (ssize_t i = 0; i < count; i++)
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

		close(fd);
		output = nullptr;
	};

	unsigned long HiddenProcess::Finish()
	{
		int status = 0;

		waitpid(static_cast<pid_t>(ToInt(process)), &status, 0);
		process = nullptr;

		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	};
};

#endif

#ifndef __EMSCRIPTEN__

namespace Loom
{
	bool HiddenProcess::Run(const std::string& command_line, const std::function<void(const std::string&)>& on_line)
	{
		HiddenProcess hidden;

		if (!hidden.Start(command_line))
			return false;

		hidden.ReadLines(on_line);

		return hidden.Finish() == 0;
	};
};

#endif

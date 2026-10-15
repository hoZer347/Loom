#pragma once

#include <functional>
#include <string>


namespace Loom
{
	/**
	* Loom::HiddenProcess
	* - Runs a command line with no console window, its stdout and stderr on
	*   one pipe. The editor has no console, so a console program it starts
	*   any other way opens a window of its own
	* - Only the pipe is inherited. Inheriting everything would hand one
	*   child whatever other pipe the editor has open at that moment, and
	*   the other's reader would never see its end
	*/
	struct HiddenProcess final
	{
		void* process = nullptr;
		void* output = nullptr;

		// Given a job, the process is in it before it runs. False when it could
		// not start; then neither handle is set.
		bool Start(std::string command_line, void* job = nullptr);

		// Hands on_line each line written to the pipe, without its line ending,
		// until everything holding the pipe has exited, then closes it.
		void ReadLines(const std::function<void(const std::string&)>& on_line);

		// Waits for the process and closes it, giving its exit code.
		unsigned long Finish();

		// Start, ReadLines and Finish: true when it ran and exited with 0.
		static bool Run(const std::string& command_line, const std::function<void(const std::string&)>& on_line);
	};
};

#pragma once

#include "HiddenProcess.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>


namespace Loom
{
	/**
	* Loom::ScriptRun
	* - Runs one of the engine folder's PowerShell scripts with no window, it
	*   and everything it starts in a job object of its own, so starting
	*   another or closing the editor takes all of it down
	* - What it says goes to a reader on a thread of its own, which is told
	*   whether Stop is what ended it, and finishes the process once its
	*   output has run out
	*/
	struct ScriptRun final
	{
		using Reader = std::function<void(HiddenProcess, const std::atomic<bool>& stopping)>;

		ScriptRun() = default;
		~ScriptRun();

		ScriptRun(const ScriptRun&) = delete;
		ScriptRun& operator=(const ScriptRun&) = delete;

		// arguments follow the script's path on its command line. Stops
		// whatever an earlier one left running.
		bool Start(const std::string& script, const std::string& arguments, const Reader& read);

		void Stop();

		// Quoted for a command line. A trailing backslash would escape the
		// closing quote, and a folder means the same without one.
		static std::string Quote(std::string argument)
		{
			while (!argument.empty() && (argument.back() == '\\' || argument.back() == '/'))
				argument.pop_back();

			return '"' + argument + '"';
		};

	private:
		void* m_job = nullptr;
		std::thread m_reader{ };

		// Set while Stop takes the script down, which is no failure of its own.
		std::atomic<bool> m_stopping = false;
	};
};

#pragma once

#include "Loom API.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>


namespace Loom
{
	/**
	* Loom::Lane
	* - A thread of its own that runs one piece of work at a time beside
	*   whatever started it, such as the objects set to a thread beside
	*   those on the main one
	* - Start hands it work and returns at once; Wait returns once that work
	*   is done, and the work is started again only after
	* - Named in the profiler, so the Stats panel shows it as a lane of its own
	*/
	struct LOOM_API Lane final
	{
		typedef std::function<void()> Work;

		// order places it among the profiler's threads.
		Lane(const std::string& name, int order);
		~Lane();

		Lane(const Lane&) = delete;
		Lane& operator=(const Lane&) = delete;

		void Start(const Work& work);
		void Wait();

		bool Busy() const { return m_busy; };

		// Whether this is the thread asking, which must not wait on itself.
		bool Running() const { return std::this_thread::get_id() == m_thread.get_id(); };

		// Whether the thread asking is any lane's.
		static bool OnOne();

		// Whether any lane has work started and not yet done.
		static bool AnyBusy();

	private:
		void Run(const std::string& name, int order);

		Work m_work{ };

		std::atomic<unsigned> m_generation{ 0 };
		std::atomic<bool> m_busy{ false };
		std::atomic<bool> m_stopping{ false };

		std::thread m_thread;
	};
};

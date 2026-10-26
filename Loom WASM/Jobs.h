#pragma once

#include "Loom API.h"

#include <atomic>
#include <cstddef>
#include <functional>


namespace Loom
{
	/**
	* Loom::Jobs
	* - A pool of worker threads, one fewer than the machine has cores, or on
	*   the web than the threads the browser runs at once, for work that
	*   splits into independent pieces. The calling thread works alongside
	*   them, and For returns once every piece is done
	* - The pieces are chunks of a range, numbered in order, so what each
	*   chunk writes to its own slot can be put together the same way every
	*   run, however the threads happened to share them out
	* - Without other threads to share with, everything runs on the caller.
	*   Fors from different threads take turns, and work must not start
	*   another
	*/
	struct LOOM_API Jobs final
	{
		#pragma region Work

		typedef std::function<void(size_t chunk, size_t begin, size_t end)> Work;

		// Runs work over [0, count) in chunks of grain, the last one shorter. A
		// grain of 0 is taken as 1.
		static void For(size_t count, size_t grain, const Work& work);

		static size_t Chunks(size_t count, size_t grain)
		{
			const size_t step = grain == 0 ? 1 : grain;

			return count / step + (count % step != 0 ? 1 : 0);
		};

		// Spins a while, then sleeps, until counter reaches at least value,
		// for work split so its pieces wait on the few they follow rather than
		// on all. Whatever raises counter notifies it.
		static void WaitFor(const std::atomic<int>& counter, int value);

		#pragma endregion

		#pragma region Threads

		// The caller and the workers the pool was made with. Unlike Threads,
		// the same however many have started and whatever the limit, so work
		// split by it splits the same way every run on a machine.
		static unsigned Cores();

		// The caller and the workers running, within the limit.
		static unsigned Threads();

		// At most this many threads share a For, the caller among them, so a
		// test can compare what fewer give; none for no limit.
		static void SetMostThreads(unsigned most);

		#pragma endregion
	};
};

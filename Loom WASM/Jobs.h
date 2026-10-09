#pragma once

#include "Loom API.h"

#include <cstddef>
#include <functional>


namespace Loom
{
	/**
	* Loom::Jobs
	* - A pool of worker threads, one fewer than the machine runs at once, for
	*   work that splits into independent pieces. The calling thread works
	*   alongside them, and For returns once every piece is done
	* - The pieces are chunks of a range, numbered in order, so what each
	*   chunk writes to its own slot can be put together the same way every
	*   run, however the threads happened to share them out
	* - Without other threads to share with, everything runs on the caller.
	*   One For at a time: work must not start another
	*/
	struct LOOM_API Jobs final
	{
		typedef std::function<void(size_t chunk, size_t begin, size_t end)> Work;

		// Runs work over [0, count) in chunks of grain, the last one shorter.
		static void For(size_t count, size_t grain, const Work& work);

		static size_t Chunks(size_t count, size_t grain) { return (count + grain - 1) / grain; };

		// The caller and the workers.
		static unsigned Threads();
	};
};

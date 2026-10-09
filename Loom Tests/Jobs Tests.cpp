#include "doctest.h"

#include "Jobs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>


namespace
{
	constexpr size_t GRAIN = 16;

	// Runs a For over count and records how often each index and each chunk
	// was given out, and whether every chunk got the range it should.
	struct Tally final
	{
		explicit Tally(size_t count) :
			indices(new std::atomic<int>[count]()),
			chunks(new std::atomic<int>[Loom::Jobs::Chunks(count, GRAIN)]()),
			count(count)
		{
			Loom::Jobs::For(count, GRAIN,
				[&](size_t chunk, size_t begin, size_t end)
				{
					chunks[chunk]++;

					if (begin != chunk * GRAIN || end != std::min(begin + GRAIN, count))
						misplaced = true;

					for (size_t i = begin; i < end; i++)
						indices[i]++;
				});
		};

		bool EachOnce() const
		{
			for (size_t i = 0; i < count; i++)
				if (indices[i] != 1)
					return false;

			for (size_t chunk = 0; chunk < Loom::Jobs::Chunks(count, GRAIN); chunk++)
				if (chunks[chunk] != 1)
					return false;

			return !misplaced;
		};

		std::unique_ptr<std::atomic<int>[]> indices;
		std::unique_ptr<std::atomic<int>[]> chunks;
		size_t count;
		std::atomic<bool> misplaced{ false };
	};
};

TEST_SUITE("Jobs")
{
	TEST_CASE("For gives out every chunk once, with its own range, whatever the count")
	{
		constexpr size_t COUNTS[] = { 0, 1, GRAIN - 1, GRAIN, GRAIN + 1, GRAIN * 64, GRAIN * 64 + 3 };

		for (size_t count : COUNTS)
		{
			CAPTURE(count);
			CHECK(Tally(count).EachOnce());
		};
	};

	TEST_CASE("many Fors back to back each give out every chunk once")
	{
		constexpr int ROUNDS = 2000;
		constexpr size_t COUNT = GRAIN * 8;

		int good = 0;

		for (int round = 0; round < ROUNDS; round++)
			good += Tally(COUNT).EachOnce();

		CHECK(good == ROUNDS);
	};

	TEST_CASE("a limit of one thread runs every chunk on the caller")
	{
		constexpr size_t COUNT = GRAIN * 32;

		Loom::Jobs::SetMostThreads(1);
		CHECK(Loom::Jobs::Threads() == 1);

		const std::thread::id caller = std::this_thread::get_id();
		std::atomic<int> elsewhere{ 0 };

		Loom::Jobs::For(COUNT, GRAIN,
			[&](size_t, size_t, size_t)
			{
				elsewhere += std::this_thread::get_id() != caller;
			});

		Loom::Jobs::SetMostThreads(0);

		CHECK(elsewhere == 0);
		CHECK(Loom::Jobs::Threads() >= 1);
	};

	TEST_CASE("the pool's cores stay put whatever the limit, and cover the threads running")
	{
		const unsigned cores = Loom::Jobs::Cores();

		CHECK(cores >= 1);
		CHECK(cores >= Loom::Jobs::Threads());

		Loom::Jobs::SetMostThreads(1);

		CHECK(Loom::Jobs::Cores() == cores);
		CHECK(Loom::Jobs::Threads() == 1);

		Loom::Jobs::SetMostThreads(0);

		CHECK(Loom::Jobs::Cores() == cores);
		CHECK(cores >= Loom::Jobs::Threads());
	};

	TEST_CASE("WaitFor returns at once for a counter already there")
	{
		std::atomic<int> counter{ 3 };

		Loom::Jobs::WaitFor(counter, 3);
		Loom::Jobs::WaitFor(counter, 1);

		CHECK(counter == 3);
	};

// Without -pthread, emscripten's std::thread throws rather than running
// anything. Both web builds link with it.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
	TEST_CASE("WaitFor sleeps until another thread raises the counter and says so")
	{
		// Long enough that the waiter has stopped spinning and is asleep.
		constexpr std::chrono::milliseconds LATER{ 50 };

		std::atomic<int> counter{ 0 };
		std::atomic<bool> raised{ false };

		std::thread raiser(
			[&]()
			{
				std::this_thread::sleep_for(LATER);

				raised = true;
				counter = 1;
				counter.notify_all();
			});

		Loom::Jobs::WaitFor(counter, 1);
		const bool afterwards = raised;

		raiser.join();

		CHECK(afterwards);
	};
#endif
};

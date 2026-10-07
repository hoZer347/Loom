#include "doctest.h"

#include "Test Support.h"

#include "Engine.h"
#include "LoomObject.h"
#include "SerializedField.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using LoomTests::Pump;


// Without -pthread, emscripten's std::thread throws rather than running
// anything. Both web builds link with it.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
namespace
{
	constexpr int threads = 4;

	// Long enough that a busy machine is not a failure, short enough that a
	// build with no real threads fails rather than hangs.
	constexpr std::chrono::seconds rendezvous_timeout{ 10 };

	struct Threaded final : Loom::LoomObject
	{
		LOOM_SERIAL(int, count);
		LOOM_SERIAL(std::string, label);
	};
};

TEST_SUITE("Threading")
{
	// Each thread waits to see every other one arrive. That only finishes if
	// they are all running at once: threads run one after another on a single
	// core, or queued behind a pool with no workers, time out instead.
	TEST_CASE("threads run at the same time")
	{
		std::atomic<int> arrived = 0;
		std::atomic<int> met = 0;
		std::vector<std::thread> workers;

		for (int t = 0; t < threads; t++)
			workers.emplace_back(
				[&arrived, &met]()
				{
					arrived++;

					const auto deadline = std::chrono::steady_clock::now() + rendezvous_timeout;

					while (arrived < threads && std::chrono::steady_clock::now() < deadline)
						std::this_thread::yield();

					if (arrived == threads)
						met++;
				});

		for (std::thread& worker : workers)
			worker.join();

		CHECK(met == threads);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "work queued from other threads runs on the thread that drains the queue")
	{
		constexpr int per_thread = 250;

		const std::thread::id draining = std::this_thread::get_id();

		std::vector<std::vector<int>> ran(threads);
		std::atomic<int> elsewhere = 0;
		std::vector<std::thread> workers;

		for (int t = 0; t < threads; t++)
			workers.emplace_back(
				[&ran, &elsewhere, draining, t]()
				{
					for (int i = 0; i < per_thread; i++)
						Loom::Engine::QueueTask(
							[&ran, &elsewhere, draining, t, i]()
							{
								if (std::this_thread::get_id() != draining)
									elsewhere++;

								ran[t].push_back(i);
							});
				});

		for (std::thread& worker : workers)
			worker.join();

		Pump();

		CHECK(elsewhere == 0);

		// Interleaved between threads however they raced, but each thread's
		// own tasks in the order it queued them.
		for (const std::vector<int>& fromThread : ran)
		{
			REQUIRE(fromThread.size() == per_thread);

			for (int i = 0; i < per_thread; i++)
				CHECK(fromThread[i] == i);
		};
	};

	// A Serial member finds its owner through whichever object's constructor
	// last started on its own thread. Objects built side by side on different
	// threads must each end up with their own fields, pointing into themselves.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "objects built on other threads keep their own fields")
	{
		constexpr int per_thread = 100;
		constexpr size_t fields = 2;

		std::vector<std::vector<std::unique_ptr<Threaded>>> built(threads);
		std::vector<std::thread> workers;

		for (int t = 0; t < threads; t++)
			workers.emplace_back(
				[&built, t]()
				{
					for (int i = 0; i < per_thread; i++)
						built[t].push_back(std::make_unique<Threaded>());
				});

		for (std::thread& worker : workers)
			worker.join();

		// Their registration runs on the queue; the objects have to outlive it.
		Pump();

		for (const auto& fromThread : built)
			for (const std::unique_ptr<Threaded>& object : fromThread)
			{
				const char* const begin = reinterpret_cast<const char*>(object.get());
				const char* const end = begin + sizeof(Threaded);

				REQUIRE(object->GetFields().size() == fields);

				for (const Loom::SerializedField& field : object->GetFields())
				{
					const char* const data = static_cast<const char*>(field.data);

					CHECK((data >= begin && data < end));
				};

				CHECK(Loom::LoomObject::GetByGuid<Threaded>(object->GetGuid()) == object.get());
			};
	};
};
#endif

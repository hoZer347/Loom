#include "doctest.h"

#include "Engine.h"
#include "Lane.h"

#include <atomic>
#include <chrono>
#include <thread>


// Without -pthread, emscripten's std::thread throws rather than running
// anything. Both web builds link with it.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
TEST_SUITE("Lane")
{
	TEST_CASE("work runs on the lane's own thread, and Wait returns once it is done")
	{
		// Long enough that Wait has gone to sleep before the work ends.
		constexpr std::chrono::milliseconds WORKING{ 50 };

		Loom::Lane lane("Test", 0);

		std::thread::id ranOn{ };
		bool running = false;

		lane.Start(
			[&]()
			{
				std::this_thread::sleep_for(WORKING);

				ranOn = std::this_thread::get_id();
				running = lane.Running();
			});

		CHECK(lane.Busy());
		CHECK_FALSE(lane.Running());

		lane.Wait();

		CHECK_FALSE(lane.Busy());
		CHECK(ranOn != std::this_thread::get_id());
		CHECK(running);
	};

	TEST_CASE("each start runs its work once, in turn")
	{
		constexpr int STARTS = 100;

		Loom::Lane lane("Test", 0);
		int runs = 0;

		for (int start = 0; start < STARTS; start++)
			lane.Start([&]() { runs++; });

		lane.Wait();

		CHECK(runs == STARTS);
	};

	TEST_CASE("a lane leaves the task queue to the main thread")
	{
		bool ran = false;
		Loom::Engine::QueueTask([&]() { ran = true; });

		Loom::Lane lane("Test", 0);

		lane.Start([]() { Loom::Engine::DoTasks(); });
		lane.Wait();

		CHECK_FALSE(ran);
		CHECK_FALSE(Loom::Lane::OnOne());

		Loom::Engine::DoTasks();

		CHECK(ran);
	};

	TEST_CASE("Wait on a lane given nothing returns at once")
	{
		Loom::Lane lane("Test", 0);

		lane.Wait();

		CHECK_FALSE(lane.Busy());
	};
};
#endif

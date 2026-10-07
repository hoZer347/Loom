#include "doctest.h"

#include "Test Support.h"

#include "Engine.h"

#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using LoomTests::Pump;


TEST_SUITE("Engine")
{
	// Engine's constructor opens a window and a renderer, so these cover the
	// static half of Engine that a host can reach without one.
	TEST_CASE("an unstarted engine reports itself as not running")
	{
		CHECK_FALSE(Loom::Engine::isRunning);
		CHECK(Loom::Engine::window == nullptr);
	};

	TEST_CASE("GetUniqueID never hands out the same ID twice")
	{
		std::set<size_t> seen;

		for (int i = 0; i < 1000; i++)
			CHECK(seen.insert(Loom::Engine::GetUniqueID()).second);
	};

	// Without -pthread, emscripten's std::thread throws rather than running
	// anything. run-web-tests.ps1 builds with it.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
	TEST_CASE("GetUniqueID stays unique across threads")
	{
		constexpr int threads = 4;
		constexpr int perThread = 500;

		std::vector<std::vector<size_t>> claimed(threads);
		std::vector<std::thread> workers;

		for (int t = 0; t < threads; t++)
			workers.emplace_back(
				[&claimed, t]()
				{
					claimed[t].reserve(perThread);
					for (int i = 0; i < perThread; i++)
						claimed[t].push_back(Loom::Engine::GetUniqueID());
				});

		for (std::thread& worker : workers)
			worker.join();

		std::set<size_t> seen;
		for (const std::vector<size_t>& fromThread : claimed)
			for (size_t id : fromThread)
				CHECK(seen.insert(id).second);

		CHECK(seen.size() == threads * perThread);
	};
#endif

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "QueueTask defers work until DoTasks runs it")
	{
		bool ran = false;

		Loom::Engine::QueueTask([&ran]() { ran = true; });

		CHECK_FALSE(ran);

		Pump();

		CHECK(ran);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "queued tasks run in the order they were queued")
	{
		std::vector<int> order;

		for (int i = 0; i < 5; i++)
			Loom::Engine::QueueTask([&order, i]() { order.push_back(i); });

		Pump();

		CHECK(order == std::vector<int>{ 0, 1, 2, 3, 4 });
	};

	// Attaching a component from inside OnAttach is normal, so work queued
	// during a drain has to be picked up by that same drain rather than being
	// left for the next frame.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a task may queue more work and still see it run")
	{
		std::vector<int> order;

		Loom::Engine::QueueTask(
			[&order]()
			{
				order.push_back(1);
				Loom::Engine::QueueTask([&order]() { order.push_back(2); });
			});

		Pump();

		CHECK(order == std::vector<int>{ 1, 2 });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "DoTasks on an empty queue is a no-op")
	{
		Pump();
		Pump();

		CHECK(true); // Reaching here without hanging or crashing is the assertion.
	};

	// DoTasks is noexcept and runs from the frame loop, so a task that throws
	// must not take the process with it, and must not strand the tasks behind it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a throwing task does not stop the drain")
	{
		bool afterRan = false;

		Loom::Engine::QueueTask([]() { throw std::runtime_error("task blew up"); });
		Loom::Engine::QueueTask([&afterRan]() { afterRan = true; });

		Pump();

		CHECK(afterRan);
	};

	TEST_CASE("the frame loop defaults have the engine driving its own scenes")
	{
		CHECK(Loom::Engine::doGUI);
		CHECK(Loom::Engine::updateScenes);
		CHECK(Loom::Engine::renderScenes);
	};

	TEST_CASE("the default clear colour is the engine's dark teal")
	{
		CHECK(Loom::Engine::clearColor[0] == doctest::Approx(0.2f));
		CHECK(Loom::Engine::clearColor[1] == doctest::Approx(0.3f));
		CHECK(Loom::Engine::clearColor[2] == doctest::Approx(0.3f));
		CHECK(Loom::Engine::clearColor[3] == doctest::Approx(1.0f));
	};

	TEST_CASE("the clear colour is settable by a host")
	{
		const float original[4] =
		{
			Loom::Engine::clearColor[0], Loom::Engine::clearColor[1],
			Loom::Engine::clearColor[2], Loom::Engine::clearColor[3],
		};

		Loom::Engine::clearColor[0] = 1.0f;
		CHECK(Loom::Engine::clearColor[0] == doctest::Approx(1.0f));

		for (int i = 0; i < 4; i++)
			Loom::Engine::clearColor[i] = original[i];
	};

	TEST_CASE("there is no renderer before an engine opens a window")
	{
		// Engine's constructor is what creates one, on the backend asked for.
		CHECK(Loom::Renderer::Get() == nullptr);
		CHECK(Loom::Engine::backend == Loom::Backend::OpenGL);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "SetUpdateFunction registers a per-frame callback")
	{
		// onUpdate is only invoked from renderFrame, which needs a context, so
		// this covers the registration path rather than the call. Note that
		// renderFrame calls onUpdate unconditionally: handing it an empty Task
		// would throw std::bad_function_call on the next frame, so a host that
		// wants to stop updating has to install a no-op, not an empty one.
		bool called = false;

		Loom::Engine::SetUpdateFunction([&called]() { called = true; });
		Loom::Engine::SetUpdateFunction([]() { });

		CHECK_FALSE(called);
	};
};

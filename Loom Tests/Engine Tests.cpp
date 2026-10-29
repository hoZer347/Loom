#include "doctest.h"

#include "Test Support.h"

#include "Engine.h"

#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef __EMSCRIPTEN__

#include "OpenGL.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>

#endif

using LoomTests::Pump;


TEST_SUITE("Engine")
{
	#pragma region Unique IDs

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

	#pragma endregion

	#pragma region Tasks

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

	// Attaching from OnAttach queues work the same drain has to run.
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
		const LoomTests::Captured captured(std::cerr);
		bool afterRan = false;

		Loom::Engine::QueueTask([]() { throw std::runtime_error("task blew up"); });
		Loom::Engine::QueueTask([&afterRan]() { afterRan = true; });

		Pump();

		CHECK(afterRan);
		CHECK(captured.Says("Queued task threw: task blew up"));
	};

	#pragma endregion

	#pragma region Frame Loop

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
		// onUpdate runs only from renderFrame, which needs a context, so this checks registration.
		bool called = false;

		Loom::Engine::SetUpdateFunction([&called]() { called = true; });
		Loom::Engine::SetUpdateFunction([]() { });

		CHECK_FALSE(called);
	};

	TEST_CASE("Crop cuts a region from the top left out of a capture stored bottom row first, clipped to it")
	{
		constexpr int width = 3;
		constexpr int height = 2;
		constexpr int channels = 3;

		// Every byte its own index, so each pixel can be told from the rest.
		std::vector<uint8_t> rgb(width * height * channels);
		for (size_t i = 0; i < rgb.size(); i++)
			rgb[i] = (uint8_t)i;

		const auto pixel = [&rgb](int stored_row, int column)
		{
			const size_t at = ((size_t)stored_row * width + column) * channels;
			return std::vector<uint8_t>(rgb.begin() + at, rgb.begin() + at + channels);
		};

		int cutWidth = width;
		int cutHeight = height;
		const std::vector<uint8_t> top = Loom::Engine::Crop({ 1, 0, 2, 1 }, cutWidth, cutHeight, rgb);

		CHECK(cutWidth == 2);
		CHECK(cutHeight == 1);

		std::vector<uint8_t> expected = pixel(1, 1);
		const std::vector<uint8_t> next = pixel(1, 2);
		expected.insert(expected.end(), next.begin(), next.end());
		CHECK(top == expected);

		cutWidth = width;
		cutHeight = height;
		const std::vector<uint8_t> clipped = Loom::Engine::Crop({ 2, 1, 5, 5 }, cutWidth, cutHeight, rgb);

		CHECK(cutWidth == 1);
		CHECK(cutHeight == 1);
		CHECK(clipped == pixel(0, 2));

		cutWidth = width;
		cutHeight = height;
		CHECK(Loom::Engine::Crop({ width, 0, 1, 1 }, cutWidth, cutHeight, rgb).empty());
		CHECK(cutWidth == 0);
	};

	#pragma endregion

	#pragma region Event Poll

	TEST_CASE("the engine is not in its event poll outside a frame")
	{
		CHECK_FALSE(Loom::Engine::IsPolling());
	};

#ifndef __EMSCRIPTEN__

	constexpr int poll_window_size = 64;
	constexpr WPARAM typed = 'a';

	int s_typed = 0;
	bool s_pollingInside = false;
	bool s_leftQueued = false;

	// Stands in for the OS's own loop, which a poll from inside it must leave its messages to.
	void TypeInsidePoll(GLFWwindow* window, unsigned int)
	{
		if (s_typed++ > 0)
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		s_pollingInside = Loom::Engine::IsPolling();

		PostMessageW(hwnd, WM_CHAR, typed, 0);
		Loom::Engine::PollEvents();

		MSG message;
		s_leftQueued = PeekMessageW(&message, hwnd, WM_CHAR, WM_CHAR, PM_NOREMOVE);
	};

	TEST_CASE("a poll from inside the poll leaves the events to the outer one")
	{
		glfwInit();
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		GLFWwindow* window = glfwCreateWindow(poll_window_size, poll_window_size, "Engine", nullptr, nullptr);
		glfwDefaultWindowHints();
		glfwSetCharCallback(window, TypeInsidePoll);

		PostMessageW(glfwGetWin32Window(window), WM_CHAR, typed, 0);
		Loom::Engine::PollEvents();

		CHECK(s_pollingInside);
		CHECK(s_leftQueued);
		CHECK(s_typed == 2);
		CHECK_FALSE(Loom::Engine::IsPolling());

		glfwDestroyWindow(window);
		glfwTerminate();
	};

#endif

	#pragma endregion
};

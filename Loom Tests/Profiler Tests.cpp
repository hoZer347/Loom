#include "doctest.h"

#include "Jobs.h"
#include "Profiler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string>


// A final build records no stages, so there is nothing to look at.
#ifndef LOOM_FINAL
#pragma region Helpers

namespace
{
	constexpr std::chrono::milliseconds BUSY_FOR{ 2 };

	// Spins rather than sleeps, since the web's main thread cannot sleep.
	void BusyUntil(std::chrono::steady_clock::time_point until)
	{
		while (std::chrono::steady_clock::now() < until)
			;
	};

	void Busy(std::chrono::steady_clock::duration length)
	{
		BusyUntil(std::chrono::steady_clock::now() + length);
	};

	// From one poll to the next, on average.
	constexpr auto POLL_FOR = std::chrono::duration<double>(1.0 / Loom::Profiler::Rate);

	// In milliseconds, each point of the trend's share of a second.
	constexpr double POINT = 1000.0 / (double)Loom::Profiler::Points;

	struct Found
	{
		const Loom::Profiler::Thread* thread = nullptr;
		size_t row = 0;
	};

	// The first thread with a row of that name, which other tests' stages
	// never share.
	Found FindRow(const std::string& name, const std::string& thread_prefix = "")
	{
		for (const Loom::Profiler::Thread& thread : Loom::Profiler::Threads())
			if (thread.name.rfind(thread_prefix, 0) == 0)
				for (size_t row = 0; row < thread.rows.size(); row++)
					if (thread.rows[row].name == name)
						return { &thread, row };

		return { };
	};

	// Starts each test recording every frame, on a frame of its own, since
	// tests before may have filled the last with stages of theirs.
	struct NewFrame
	{
		NewFrame()
		{
			Loom::Profiler::SetEveryFrame(true);
			Loom::Profiler::SetRecording(true);
			Loom::Profiler::EndFrame();
		};

		~NewFrame()
		{
			Loom::Profiler::SetRecording(false);
			Loom::Profiler::SetEveryFrame(false);
		};
	};
};

#pragma endregion

TEST_SUITE("Profiler")
{
	#pragma region Stages

	TEST_CASE_FIXTURE(NewFrame, "a stage's parts follow it one deeper, in the order they began")
	{
		{
			LOOM_STAGE("Profiler outer");

			{
				LOOM_STAGE("Profiler first part");
				Busy(BUSY_FOR);
			};

			LOOM_STAGE("Profiler second part");
			Busy(BUSY_FOR);
		};

		Loom::Profiler::EndFrame();

		const Found outer = FindRow("Profiler outer");
		REQUIRE(outer.thread);

		const std::vector<Loom::Profiler::Row>& rows = outer.thread->rows;
		REQUIRE(outer.row + 2 < rows.size());

		CHECK(rows[outer.row + 1].name == "Profiler first part");
		CHECK(rows[outer.row + 2].name == "Profiler second part");
		CHECK(rows[outer.row + 1].depth == rows[outer.row].depth + 1);
		CHECK(rows[outer.row + 2].depth == rows[outer.row].depth + 1);

		const Loom::Profiler::Row& whole = rows[outer.row];
		const Loom::Profiler::Row& first = rows[outer.row + 1];
		const Loom::Profiler::Row& second = rows[outer.row + 2];

		CHECK(whole.average >= first.average + second.average);
		CHECK(first.average >= (double)BUSY_FOR.count());
		CHECK(first.history.back() == doctest::Approx(first.average));
	};

	TEST_CASE_FIXTURE(NewFrame, "stages are listed in the order they began in the last frame")
	{
		{
			LOOM_STAGE("Profiler later");
		};

		{
			LOOM_STAGE("Profiler sooner");
		};

		Loom::Profiler::EndFrame();

		{
			LOOM_STAGE("Profiler sooner");
		};

		{
			LOOM_STAGE("Profiler later");
		};

		Loom::Profiler::EndFrame();

		const Found sooner = FindRow("Profiler sooner");
		const Found later = FindRow("Profiler later");
		REQUIRE(sooner.thread);
		REQUIRE(later.thread == sooner.thread);
		CHECK(sooner.row < later.row);
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage run twice counts as their sum, and a frame it misses as none")
	{
		for (int run = 0; run < 2; run++)
		{
			LOOM_STAGE("Profiler twice");
			Busy(BUSY_FOR);
		};

		Loom::Profiler::EndFrame();

		const Found ran = FindRow("Profiler twice");
		REQUIRE(ran.thread);

		const double sum = ran.thread->rows[ran.row].peak;
		CHECK(sum >= 2.0 * (double)BUSY_FOR.count());
		CHECK(ran.thread->rows[ran.row].average == doctest::Approx(sum));

		Loom::Profiler::EndFrame();

		const Found missed = FindRow("Profiler twice");
		REQUIRE(missed.thread);

		const Loom::Profiler::Row& row = missed.thread->rows[missed.row];
		CHECK(row.peak == doctest::Approx(sum));
		CHECK(row.average == doctest::Approx(sum / 2.0));
		CHECK(row.history[Loom::Profiler::Window - 2] == doctest::Approx(sum));
		CHECK(row.history.back() == 0.0);
	};

	TEST_CASE_FIXTURE(NewFrame, "a point of the trend is its frames' average, a frame missed counting as none")
	{
		constexpr int frames = 10;

		Loom::Profiler::Advance(POINT);
		Loom::Profiler::EndFrame();

		for (int frame = 0; frame < frames; frame++)
		{
			if (frame % 2 == 0)
			{
				LOOM_STAGE("Profiler alternate");
				Busy(BUSY_FOR);
			};

			if (frame == frames - 1)
				Loom::Profiler::Advance(POINT);

			Loom::Profiler::EndFrame();
		};

		const Found found = FindRow("Profiler alternate");
		REQUIRE(found.thread);

		const Loom::Profiler::Row& row = found.thread->rows[found.row];
		CHECK(row.trend.back() >= (double)BUSY_FOR.count() / 2.0);
		CHECK(row.trend.back() <= doctest::Approx(row.peak / 2.0));
	};

	TEST_CASE_FIXTURE(NewFrame, "the trend gains a point each share of a second, however many frames are recorded in it")
	{
		Loom::Profiler::Advance(POINT);
		Loom::Profiler::EndFrame();

		for (int frame = 0; frame < Loom::Profiler::Window; frame++)
		{
			{
				LOOM_STAGE("Profiler many frames");
			};

			Loom::Profiler::EndFrame();
		};

		const Found waiting = FindRow("Profiler many frames");
		REQUIRE(waiting.thread);
		CHECK(waiting.thread->rows[waiting.row].trend.back() == 0.0);

		Loom::Profiler::Advance(POINT);
		Loom::Profiler::EndFrame();

		const Found closed = FindRow("Profiler many frames");
		REQUIRE(closed.thread);
		CHECK(closed.thread->rows[closed.row].trend.back() > 0.0);
	};

	TEST_CASE_FIXTURE(NewFrame, "a frame running past several points stands for each of them")
	{
		constexpr int points = 3;

		Loom::Profiler::Advance(POINT);
		Loom::Profiler::EndFrame();

		{
			LOOM_STAGE("Profiler long frame");
			Busy(BUSY_FOR);
		};

		Loom::Profiler::Advance(POINT * points);
		Loom::Profiler::EndFrame();

		const Found found = FindRow("Profiler long frame");
		REQUIRE(found.thread);

		const std::array<double, Loom::Profiler::Trend>& trend = found.thread->rows[found.row].trend;

		for (int age = 1; age <= points; age++)
			CHECK(trend[Loom::Profiler::Trend - age] >= (double)BUSY_FOR.count());

		CHECK(trend[Loom::Profiler::Trend - points - 1] == 0.0);
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage that has not run for the whole trend is forgotten")
	{
		{
			LOOM_STAGE("Profiler once");
		};

		Loom::Profiler::EndFrame();

		for (int point = 0; point < Loom::Profiler::Trend; point++)
		{
			Loom::Profiler::Advance(POINT);
			Loom::Profiler::EndFrame();
		};

		REQUIRE(FindRow("Profiler once").thread);

		Loom::Profiler::Advance(POINT);
		Loom::Profiler::EndFrame();

		CHECK_FALSE(FindRow("Profiler once").thread);
	};

	TEST_CASE_FIXTURE(NewFrame, "the stage open innermost is the current one")
	{
		CHECK(Loom::Profiler::Current() == nullptr);

		{
			LOOM_STAGE("Profiler around");

			{
				LOOM_STAGE("Profiler inside");
				CHECK(std::strcmp(Loom::Profiler::Current(), "Profiler inside") == 0);
			};

			CHECK(std::strcmp(Loom::Profiler::Current(), "Profiler around") == 0);
		};

		CHECK(Loom::Profiler::Current() == nullptr);
		Loom::Profiler::EndFrame();
	};

	#pragma endregion

	#pragma region Recording

	TEST_CASE_FIXTURE(NewFrame, "nothing is recorded while recording is off, and what was is forgotten")
	{
		{
			LOOM_STAGE("Profiler before");
		};

		Loom::Profiler::EndFrame();
		REQUIRE(FindRow("Profiler before").thread);

		Loom::Profiler::SetRecording(false);

		{
			LOOM_STAGE("Profiler unwatched");
			CHECK(Loom::Profiler::Current() == nullptr);
		};

		Loom::Profiler::EndFrame();

		CHECK(Loom::Profiler::Threads().empty());
		CHECK(Loom::Profiler::FrameLength() == 0.0);

		Loom::Profiler::SetRecording(true);
		Loom::Profiler::EndFrame();

		CHECK_FALSE(FindRow("Profiler before").thread);
		CHECK_FALSE(FindRow("Profiler unwatched").thread);
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage open as recording turns off or on closes as it began")
	{
		{
			LOOM_STAGE("Profiler stopped inside");
			Loom::Profiler::SetRecording(false);
		};

		CHECK(Loom::Profiler::Current() == nullptr);
		Loom::Profiler::EndFrame();
		CHECK(Loom::Profiler::Threads().empty());

		{
			LOOM_STAGE("Profiler started inside");
			Loom::Profiler::SetRecording(true);
			CHECK(Loom::Profiler::Current() == nullptr);

			{
				LOOM_STAGE("Profiler after start");
				CHECK(std::strcmp(Loom::Profiler::Current(), "Profiler after start") == 0);
			};
		};

		CHECK(Loom::Profiler::Current() == nullptr);
		Loom::Profiler::EndFrame();

		const Found after = FindRow("Profiler after start");
		REQUIRE(after.thread);
		CHECK(after.thread->rows[after.row].depth == 0);
		CHECK_FALSE(FindRow("Profiler stopped inside").thread);
		CHECK_FALSE(FindRow("Profiler started inside").thread);
	};

	TEST_CASE_FIXTURE(NewFrame, "short of every frame, only a frame starting once a poll is due is recorded")
	{
		constexpr auto poll = std::chrono::ceil<std::chrono::milliseconds>(POLL_FOR);

		Loom::Profiler::SetEveryFrame(false);

		{
			LOOM_STAGE("Profiler polled");
		};

		Loom::Profiler::EndFrame();
		CHECK(FindRow("Profiler polled").thread);

		{
			LOOM_STAGE("Profiler between polls");
			CHECK(Loom::Profiler::Current() == nullptr);
		};

		Loom::Profiler::EndFrame();
		CHECK_FALSE(FindRow("Profiler between polls").thread);

		Busy(poll);
		Loom::Profiler::EndFrame();

		{
			LOOM_STAGE("Profiler next poll");
		};

		Loom::Profiler::EndFrame();
		CHECK(FindRow("Profiler next poll").thread);
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage of a recorded frame closing in one that is not is dropped")
	{
		constexpr auto poll = std::chrono::ceil<std::chrono::milliseconds>(POLL_FOR);

		Loom::Profiler::SetEveryFrame(false);

		{
			LOOM_STAGE("Profiler straggler");
			Loom::Profiler::EndFrame();
		};

		Loom::Profiler::EndFrame();
		Busy(poll);
		Loom::Profiler::EndFrame();
		Loom::Profiler::EndFrame();

		CHECK_FALSE(FindRow("Profiler straggler").thread);
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage on every other frame runs in about half the recorded frames, with frames twice as fast as polls")
	{
		constexpr int polls = 40;
		constexpr int frames_per_poll = 2;
		constexpr double half = 0.5;
		constexpr double tolerance = 0.25;

		const auto frame_for = std::chrono::duration_cast<std::chrono::steady_clock::duration>(POLL_FOR / frames_per_poll);
		auto frame_end = std::chrono::steady_clock::now();

		Loom::Profiler::SetEveryFrame(false);

		for (int frame = 0; frame < polls * frames_per_poll; frame++)
		{
			frame_end += frame_for;

			if (frame % frames_per_poll == 0)
			{
				LOOM_STAGE("Profiler every other");
				Busy(BUSY_FOR);
			};

			BusyUntil(frame_end);
			Loom::Profiler::EndFrame();
		};

		const Found found = FindRow("Profiler every other");
		REQUIRE(found.thread);

		// Counted by frame rather than by time, which a busy machine stretches.
		const auto& history = found.thread->rows[found.row].history;
		const auto ran = [](double length) { return length > 0.0; };
		const auto first = std::find_if(history.begin(), history.end(), ran);
		const double share = (double)std::count_if(first, history.end(), ran) / (double)(history.end() - first);

		CHECK(share > half - tolerance);
		CHECK(share < half + tolerance);
	};

	#pragma endregion

	#pragma region Workers

	TEST_CASE_FIXTURE(NewFrame, "a worker's share of a For counts towards the stage it was started in")
	{
		if (Loom::Jobs::Threads() < 2)
			return;

		constexpr size_t CHUNKS = 64;
		constexpr std::chrono::milliseconds CHUNK_FOR{ 1 };

		{
			LOOM_STAGE("Profiler spread");

			Loom::Jobs::For(CHUNKS, 1,
				[&](size_t, size_t, size_t)
				{
					Busy(CHUNK_FOR);
				});
		};

		Loom::Profiler::EndFrame();

		const Found worker = FindRow("Profiler spread", "Worker ");
		REQUIRE(worker.thread);
		CHECK(worker.thread->rows[worker.row].depth == 0);
		CHECK(worker.thread->rows[worker.row].peak > 0.0);
	};

	#pragma endregion
};
#endif

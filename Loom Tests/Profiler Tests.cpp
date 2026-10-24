#include "doctest.h"

#include "Jobs.h"
#include "Profiler.h"

#include <chrono>
#include <cstring>
#include <string>


// A final build records no stages, so there is nothing to look at.
#ifndef LOOM_FINAL
namespace
{
	constexpr std::chrono::milliseconds BUSY_FOR{ 2 };

	// Spins rather than sleeps, since the web's main thread cannot sleep.
	void Busy(std::chrono::milliseconds length)
	{
		const auto until = std::chrono::steady_clock::now() + length;

		while (std::chrono::steady_clock::now() < until)
			;
	};

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

	// Starts each test recording, on a frame of its own, since tests before
	// may have filled the last with stages of theirs.
	struct NewFrame
	{
		NewFrame()
		{
			Loom::Profiler::SetRecording(true);
			Loom::Profiler::EndFrame();
		};

		~NewFrame()
		{
			Loom::Profiler::SetRecording(false);
		};
	};
};

TEST_SUITE("Profiler")
{
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

		CHECK(whole.start <= first.start);
		CHECK(first.start + first.average <= second.start);
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
		const double start = ran.thread->rows[ran.row].start;
		CHECK(sum >= 2.0 * (double)BUSY_FOR.count());
		CHECK(ran.thread->rows[ran.row].average == doctest::Approx(sum));

		Loom::Profiler::EndFrame();

		const Found missed = FindRow("Profiler twice");
		REQUIRE(missed.thread);

		const Loom::Profiler::Row& row = missed.thread->rows[missed.row];
		CHECK(row.peak == doctest::Approx(sum));
		CHECK(row.average == doctest::Approx(sum / 2.0));
		CHECK(row.start == doctest::Approx(start));
		CHECK(row.history[Loom::Profiler::Window - 2] == doctest::Approx(sum));
		CHECK(row.history.back() == 0.0);
	};

	TEST_CASE_FIXTURE(NewFrame, "a point of the trend is its frames' average, a frame missed counting as none")
	{
		static_assert(Loom::Profiler::Bucket % 2 == 0, "every other frame of a bucket must be half of it");

		for (int frame = 0; frame < 2 * Loom::Profiler::Bucket; frame++)
		{
			if (frame % 2 == 0)
			{
				LOOM_STAGE("Profiler alternate");
				Busy(BUSY_FOR);
			};

			Loom::Profiler::EndFrame();
		};

		const Found found = FindRow("Profiler alternate");
		REQUIRE(found.thread);

		const Loom::Profiler::Row& row = found.thread->rows[found.row];
		CHECK(row.trend.back() >= (double)BUSY_FOR.count() / 2.0);
		CHECK(row.trend.back() <= doctest::Approx(row.peak / 2.0));
	};

	TEST_CASE_FIXTURE(NewFrame, "a stage that has not run for the whole trend is forgotten")
	{
		constexpr int SPAN = Loom::Profiler::Trend * Loom::Profiler::Bucket;

		{
			LOOM_STAGE("Profiler once");
		};

		Loom::Profiler::EndFrame();

		for (int frame = 1; frame < SPAN; frame++)
			Loom::Profiler::EndFrame();

		REQUIRE(FindRow("Profiler once").thread);

		for (int frame = 0; frame < Loom::Profiler::Bucket; frame++)
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
};
#endif

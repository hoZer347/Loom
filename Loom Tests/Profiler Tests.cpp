#include "doctest.h"

#include "Jobs.h"
#include "Profiler.h"

#include <chrono>
#include <cstring>
#include <string>


// A release build records no stages, so there is nothing to look at.
#ifndef NDEBUG
namespace
{
	constexpr std::chrono::milliseconds busy_for{ 2 };

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

	const Loom::Profiler::Span* FindSpan(const Loom::Profiler::Thread& thread, const std::string& name)
	{
		for (const Loom::Profiler::Span& span : thread.spans)
			if (span.name == name)
				return &span;

		return nullptr;
	};
};

TEST_SUITE("Profiler")
{
	TEST_CASE("a stage's parts follow it one deeper, in the order they began")
	{
		{
			LOOM_STAGE("Profiler outer");

			{
				LOOM_STAGE("Profiler first part");
				Busy(busy_for);
			};

			LOOM_STAGE("Profiler second part");
			Busy(busy_for);
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

		const Loom::Profiler::Span* whole = FindSpan(*outer.thread, "Profiler outer");
		const Loom::Profiler::Span* first = FindSpan(*outer.thread, "Profiler first part");
		const Loom::Profiler::Span* second = FindSpan(*outer.thread, "Profiler second part");
		REQUIRE(whole);
		REQUIRE(first);
		REQUIRE(second);

		CHECK(whole->start <= first->start);
		CHECK(first->start + first->length <= second->start);
		CHECK(whole->length >= first->length + second->length);
		CHECK(first->length >= (double)busy_for.count());
	};

	TEST_CASE("stages are listed in the order they began in the last frame")
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

	TEST_CASE("a stage run twice counts as their sum, and a frame it misses as none")
	{
		for (int run = 0; run < 2; run++)
		{
			LOOM_STAGE("Profiler twice");
			Busy(busy_for);
		};

		Loom::Profiler::EndFrame();

		const Found ran = FindRow("Profiler twice");
		REQUIRE(ran.thread);

		const double sum = ran.thread->rows[ran.row].peak;
		CHECK(sum >= 2.0 * (double)busy_for.count());
		CHECK(ran.thread->rows[ran.row].average == doctest::Approx(sum));

		Loom::Profiler::EndFrame();

		const Found missed = FindRow("Profiler twice");
		REQUIRE(missed.thread);
		CHECK(missed.thread->rows[missed.row].peak == doctest::Approx(sum));
		CHECK(missed.thread->rows[missed.row].average == doctest::Approx(sum / 2.0));
	};

	TEST_CASE("a stage that has not run for a whole window is forgotten")
	{
		{
			LOOM_STAGE("Profiler once");
		};

		Loom::Profiler::EndFrame();

		for (int frame = 1; frame < Loom::Profiler::Window; frame++)
		{
			Loom::Profiler::EndFrame();
			REQUIRE(FindRow("Profiler once").thread);
		};

		Loom::Profiler::EndFrame();
		CHECK_FALSE(FindRow("Profiler once").thread);
	};

	TEST_CASE("the stage open innermost is the current one")
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

	TEST_CASE("a worker's share of a For counts towards the stage it was started in")
	{
		if (Loom::Jobs::Threads() < 2)
			return;

		constexpr size_t chunks = 64;
		constexpr std::chrono::milliseconds chunk_for{ 1 };

		{
			LOOM_STAGE("Profiler spread");

			Loom::Jobs::For(chunks, 1,
				[&](size_t, size_t, size_t)
				{
					Busy(chunk_for);
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

#include "doctest.h"

#include "Test Support.h"

#include "Engine.h"
#include "Scene.h"
#include "ScriptTests.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>


#pragma region Helpers

namespace
{
	// Takes back whatever a case registers, as an unloading script library
	// does.
	struct Registered
	{
		const size_t before = Loom::ScriptTests::All().size();

		~Registered()
		{
			Loom::ScriptTests::Truncate(before);
		};

		const Loom::ScriptTests::Case& Add(const std::string& name, const Loom::ScriptTests::Body& body) const
		{
			constexpr int line = 7;

			Loom::ScriptTests::Register(name, "Scripts/Laser.hpp", line, body);

			return Loom::ScriptTests::All().back();
		};
	};

	int declared_runs = 0;

	LOOM_TEST_AS("declared by the macro", declared)
	{
		++declared_runs;
		LOOM_CHECK(declared_runs > 0);
	};
};

#pragma endregion


TEST_SUITE("ScriptTests")
{
	#pragma region Cases

	TEST_CASE("LOOM_TEST registers its case with the file and line it is in")
	{
		const auto& cases = Loom::ScriptTests::All();
		const auto found = std::ranges::find(cases, std::string("declared by the macro"), &Loom::ScriptTests::Case::name);

		REQUIRE(found != cases.end());
		CHECK(found->file.find("ScriptTests Tests.cpp") != std::string::npos);
		CHECK(found->line > 0);

		const int runs = declared_runs;
		CHECK(Loom::ScriptTests::Run(*found).outcome == Loom::ScriptTests::Outcome::Passed);
		CHECK(declared_runs == runs + 1);
	};

	TEST_CASE("Truncate takes back the cases registered after the count")
	{
		const size_t before = Loom::ScriptTests::All().size();

		{
			const Registered registered;

			registered.Add("one", [] { });
			registered.Add("two", [] { });

			CHECK(Loom::ScriptTests::All().size() == before + 2);
		};

		CHECK(Loom::ScriptTests::All().size() == before);

		Loom::ScriptTests::Truncate(before + 1);
		CHECK(Loom::ScriptTests::All().size() == before);
	};

	TEST_CASE("a second case with a name already taken is left out")
	{
		const Registered registered;
		const size_t before = Loom::ScriptTests::All().size();
		bool second_ran = false;

		registered.Add("taken", [] { });

		std::ostringstream captured;
		std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());

		Loom::ScriptTests::Register("taken", "Scripts/Door.hpp", 1, [&second_ran] { second_ran = true; });

		std::cerr.rdbuf(previous);

		CHECK(Loom::ScriptTests::All().size() == before + 1);
		CHECK(Loom::ScriptTests::All().back().file == "Scripts/Laser.hpp");
		CHECK(captured.str().find("Scripts/Door.hpp:1") != std::string::npos);
		CHECK_FALSE(second_ran);
	};

	TEST_CASE("a filter matches any part of a name, whatever its case")
	{
		CHECK(Loom::ScriptTests::Matches("Laser stops at a wall", ""));
		CHECK(Loom::ScriptTests::Matches("Laser stops at a wall", "laser"));
		CHECK(Loom::ScriptTests::Matches("Laser stops at a wall", "STOPS AT"));
		CHECK_FALSE(Loom::ScriptTests::Matches("Laser stops at a wall", "door"));
		CHECK_FALSE(Loom::ScriptTests::Matches("wall", "a wall"));
	};

	#pragma endregion

	#pragma region Running

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a case with no failed check passes")
	{
		const Registered registered;

		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(registered.Add("passes", [] { LOOM_CHECK(1 + 1 == 2); }));

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Passed);
		CHECK(result.failures.empty());
		CHECK(result.milliseconds >= 0.0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a failed check is noted with its line and the case carries on")
	{
		const Registered registered;
		bool finished = false;

		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(registered.Add("fails", [&finished]
			{
				LOOM_CHECK(1 + 1 == 3);
				LOOM_CHECK(2 > 5);
				finished = true;
			}));

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Failed);
		CHECK(finished);
		REQUIRE(result.failures.size() == 2);
		CHECK(result.failures[0].text == "1 + 1 == 3");
		CHECK(result.failures[0].file.find("ScriptTests Tests.cpp") != std::string::npos);
		CHECK(result.failures[0].line > 0);
		CHECK(result.failures[1].text == "2 > 5");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a throw ends the case and points at it")
	{
		const Registered registered;

		const Loom::ScriptTests::Case& thrower = registered.Add("throws", [] { throw std::runtime_error("no laser"); });
		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(thrower);

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Threw);
		REQUIRE(result.failures.size() == 1);
		CHECK(result.failures[0].text == "Threw no laser");
		CHECK(result.failures[0].file == thrower.file);
		CHECK(result.failures[0].line == thrower.line);

		const Loom::ScriptTests::Result other = Loom::ScriptTests::Run(registered.Add("throws a number", [] { throw 3; }));

		CHECK(other.outcome == Loom::ScriptTests::Outcome::Threw);
		REQUIRE(other.failures.size() == 1);
		CHECK(other.failures[0].text == "Threw something other than a std::exception");
	};

#ifdef _MSC_VER
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a crash ends the case rather than the editor")
	{
		const Registered registered;

		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(registered.Add("crashes", []
			{
				volatile int* nowhere = nullptr;
				*nowhere = 1;
			}));

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Crashed);
		REQUIRE(result.failures.size() == 1);
		CHECK(result.failures[0].text == "Crashed");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a crash takes back the scene the case held")
	{
		const Registered registered;
		const size_t scenes = Loom::Scene::GetScenes().size();

		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(registered.Add("crashes in a scene", []
			{
				Loom::Scene scene("Crashing");
				Loom::Engine::DoTasks();

				volatile int* nowhere = nullptr;
				*nowhere = 1;
			}));

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Crashed);
		CHECK(Loom::Scene::GetScenes().size() == scenes);
	};

	TEST_CASE("a stack overflow ends the case, and the next case still has its stack")
	{
		const Registered registered;

		constexpr size_t frame_bytes = 1024;

		const std::function<int(int)> deeper = [&deeper](int depth) { volatile char frame[frame_bytes]{ }; return deeper(depth + 1) + frame[0]; };

		const Loom::ScriptTests::Result overflowed = Loom::ScriptTests::Run(registered.Add("overflows", [&deeper] { deeper(0); }));

		CHECK(overflowed.outcome == Loom::ScriptTests::Outcome::Crashed);

		const Loom::ScriptTests::Result again = Loom::ScriptTests::Run(registered.Add("overflows again", [&deeper] { deeper(0); }));

		CHECK(again.outcome == Loom::ScriptTests::Outcome::Crashed);
	};
#endif

	TEST_CASE("a check outside a run is not kept")
	{
		const Registered registered;

		LOOM_CHECK(false);

		const Loom::ScriptTests::Result result = Loom::ScriptTests::Run(registered.Add("after", [] { }));

		CHECK(result.outcome == Loom::ScriptTests::Outcome::Passed);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a case's engine work is done before it returns")
	{
		const Registered registered;
		bool ran = false;

		Loom::ScriptTests::Run(registered.Add("queues", [&ran] { Loom::Engine::QueueTask([&ran] { ran = true; }); }));

		CHECK(ran);
	};

	#pragma endregion
};

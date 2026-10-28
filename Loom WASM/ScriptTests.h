#pragma once

#include "Loom API.h"

#include <functional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::ScriptTests
	* - The test cases a project's scripts declare with LOOM_TEST, which the
	*   editor's Tests panel lists, filters and runs
	* - A case runs on the main thread between frames, with the engine's tasks
	*   done before and after it. LOOM_CHECK notes a failure and carries on; a
	*   throw or a crash ends the case
	* - A final build registers none
	*/
	struct LOOM_API ScriptTests final
	{
		#pragma region Cases

		typedef std::function<void()> Body;

		struct Case
		{
			std::string name;
			std::string file;
			int line = 0;
			Body body;
		};

		// What LOOM_TEST runs as the scripts load. A name already taken is
		// left out, as the panel knows a case by its name. Always true, for
		// the static it initialises.
		static bool Register(const std::string& name, const char* file, int line, const Body& body);

		// In the order they registered.
		static const std::vector<Case>& All();

		// The case of this name, or null. Good until the next Register or Truncate.
		static const Case* Find(const std::string& name);

		// Counts each change to the cases, so a list made from them knows when it is out of date.
		static size_t Generation();

		// Takes back every case after the first count, as a script library
		// that registered them unloads.
		static void Truncate(size_t count);

		// Whether the name holds the filter, whatever the case. An empty
		// filter matches every name.
		static bool Matches(const std::string& name, const std::string& filter);

		#pragma endregion

		#pragma region Running

		// A LOOM_CHECK that did not hold, or what a case threw.
		struct Failure
		{
			std::string text;
			std::string file;
			int line = 0;
		};

		enum struct Outcome { Passed, Failed, Threw, Crashed };

		struct Result
		{
			Outcome outcome = Outcome::Passed;
			std::vector<Failure> failures;
			double milliseconds = 0.0;
		};

		static Result Run(const Case& test);

		// What LOOM_CHECK calls when its expression is false. Nothing outside
		// a run.
		static void Fail(const char* text, const char* file, int line);

		#pragma endregion
	};
};

#define LOOM_TEST_JOIN(a, b) a##b
#define LOOM_TEST_NAMED(a, b) LOOM_TEST_JOIN(a, b)

#ifdef LOOM_FINAL
#define LOOM_TEST_AS(name, id) \
	[[maybe_unused]] static void LOOM_TEST_NAMED(LoomTest, id)()
#else
#define LOOM_TEST_AS(name, id) \
	static void LOOM_TEST_NAMED(LoomTest, id)(); \
	static const bool LOOM_TEST_NAMED(LoomTestRegistered, id) = Loom::ScriptTests::Register(name, __FILE__, __LINE__, &LOOM_TEST_NAMED(LoomTest, id)); \
	static void LOOM_TEST_NAMED(LoomTest, id)()
#endif

// LOOM_TEST("a laser stops at the first shape") { ... };
#define LOOM_TEST(name) LOOM_TEST_AS(name, __COUNTER__)

#define LOOM_CHECK(expression) \
	((expression) ? (void)0 : Loom::ScriptTests::Fail(#expression, __FILE__, __LINE__))

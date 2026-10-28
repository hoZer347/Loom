#include "ScriptTests.h"

#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>

#ifdef _MSC_VER
#include <excpt.h>
#endif


namespace Loom
{
	namespace
	{
		std::vector<ScriptTests::Case>& Cases()
		{
			static std::vector<ScriptTests::Case> cases;

			return cases;
		};

		// Where LOOM_CHECK writes while a case runs, and null otherwise.
		std::vector<ScriptTests::Failure>* running = nullptr;

		// Runs the body, giving what it threw as a failure.
		ScriptTests::Outcome Catching(const ScriptTests::Body& body, std::vector<ScriptTests::Failure>& failures)
		{
			try
			{
				body();
				return ScriptTests::Outcome::Passed;
			}
			catch (const std::exception& exception)
			{
				failures.push_back({ std::string("Threw ") + exception.what() });
			}
			catch (...)
			{
				failures.push_back({ "Threw something other than a std::exception" });
			};

			return ScriptTests::Outcome::Threw;
		};

		// Catching, with a crash ending the case rather than the editor.
		ScriptTests::Outcome Guarded(const ScriptTests::Body& body, std::vector<ScriptTests::Failure>& failures)
		{
#ifdef _MSC_VER
			__try
			{
				return Catching(body, failures);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return ScriptTests::Outcome::Crashed;
			};
#else
			return Catching(body, failures);
#endif
		};
	};

	#pragma region Cases

	bool ScriptTests::Register(const std::string& name, const char* file, int line, const Body& body)
	{
		Cases().push_back({ name, file, line, body });

		return true;
	};

	const std::vector<ScriptTests::Case>& ScriptTests::All()
	{
		return Cases();
	};

	void ScriptTests::Truncate(size_t count)
	{
		if (count < Cases().size())
			Cases().resize(count);
	};

	bool ScriptTests::Matches(const std::string& name, const std::string& filter)
	{
		const auto lower = [](char c) { return (char)std::tolower((unsigned char)c); };

		return filter.empty() ||
			std::search(name.begin(), name.end(), filter.begin(), filter.end(), [&lower](char a, char b) { return lower(a) == lower(b); }) != name.end();
	};

	#pragma endregion

	#pragma region Running

	ScriptTests::Result ScriptTests::Run(const Case& test)
	{
		Result result;
		const auto started = std::chrono::steady_clock::now();

		Engine::DoTasks();

		running = &result.failures;
		result.outcome = Guarded(test.body, result.failures);
		running = nullptr;

		Engine::DoTasks();

		if (result.outcome == Outcome::Crashed)
			result.failures.push_back({ "Crashed" });
		else if (result.outcome == Outcome::Passed && !result.failures.empty())
			result.outcome = Outcome::Failed;

		// A throw or a crash has no line of its own, so it points at the case.
		for (Failure& failure : result.failures)
			if (failure.file.empty())
			{
				failure.file = test.file;
				failure.line = test.line;
			};

		result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

		return result;
	};

	void ScriptTests::Fail(const char* text, const char* file, int line)
	{
		if (running)
			running->push_back({ text, file, line });
	};

	#pragma endregion
};

#include "ScriptTests.h"

#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <iostream>
#include <unordered_map>

#ifdef _MSC_VER
#include <eh.h>
#include <excpt.h>
#include <malloc.h>
#endif


namespace Loom
{
	namespace
	{
		struct Registry
		{
			std::vector<ScriptTests::Case> cases;

			// Each case's place in cases, by its name.
			std::unordered_map<std::string, size_t> places;

			size_t generation = 0;
		};

		Registry& Cases()
		{
			static Registry registry;

			return registry;
		};

		// Where LOOM_CHECK writes while a case runs, and null otherwise.
		std::vector<ScriptTests::Failure>* running = nullptr;

		// What a crash is thrown as, so the case's locals unwind.
		struct Crash { };

		// Runs the body, giving what it threw as a failure.
		ScriptTests::Outcome Catching(const ScriptTests::Body& body, std::vector<ScriptTests::Failure>& failures)
		{
			try
			{
				body();
				return ScriptTests::Outcome::Passed;
			}
			catch (const Crash&)
			{
				return ScriptTests::Outcome::Crashed;
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

#ifdef _MSC_VER
		constexpr unsigned long STACK_OVERFLOW = 0xC00000FD;

		// A stack overflow is left to Guarded, as there is no stack left to unwind with.
		void Translate(unsigned int code, _EXCEPTION_POINTERS*)
		{
			if (code != STACK_OVERFLOW)
				throw Crash{ };
		};
#endif

		// Catching, with a crash ending the case rather than the editor.
		ScriptTests::Outcome Guarded(const ScriptTests::Body& body, std::vector<ScriptTests::Failure>& failures)
		{
#ifdef _MSC_VER
			const _se_translator_function previous = _set_se_translator(Translate);
			ScriptTests::Outcome outcome = ScriptTests::Outcome::Crashed;

			__try
			{
				outcome = Catching(body, failures);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				if (GetExceptionCode() == STACK_OVERFLOW)
					_resetstkoflw();
			};

			_set_se_translator(previous);

			return outcome;
#else
			return Catching(body, failures);
#endif
		};
	};

	#pragma region Cases

	bool ScriptTests::Register(const std::string& name, const char* file, int line, const Body& body)
	{
		Registry& registry = Cases();

		if (!registry.places.try_emplace(name, registry.cases.size()).second)
			std::cerr << "A second test named '" << name << "' at " << file << ":" << line << " is left out" << std::endl;
		else
		{
			registry.cases.push_back({ name, file, line, body });
			++registry.generation;
		};

		return true;
	};

	const std::vector<ScriptTests::Case>& ScriptTests::All()
	{
		return Cases().cases;
	};

	const ScriptTests::Case* ScriptTests::Find(const std::string& name)
	{
		const Registry& registry = Cases();
		const auto found = registry.places.find(name);

		return found == registry.places.end() ? nullptr : &registry.cases[found->second];
	};

	size_t ScriptTests::Generation()
	{
		return Cases().generation;
	};

	void ScriptTests::Truncate(size_t count)
	{
		Registry& registry = Cases();

		if (count >= registry.cases.size())
			return;

		for (size_t i = count; i < registry.cases.size(); ++i)
			registry.places.erase(registry.cases[i].name);

		registry.cases.resize(count);
		++registry.generation;
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

// Defines doctest's main; --progress-log=<file> writes each case's name and seconds there.

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "Test Support.h"
#include "Render Support.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "CrashReports.h"
#include "Executable.h"

#include <cstdlib>
#include <functional>
#include <future>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#include <process.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#endif

// What holds back the startup clash below is initialised before anything else
// in the suite or the engine, so it sees all they write.
#ifdef _MSC_VER
#pragma warning(suppress : 4073)
#pragma init_seg(lib)
#define LOOM_FIRST_INITIALISED
#else
// The first priority a program may use; the C++ library's streams come before it.
#define LOOM_FIRST_INITIALISED __attribute__((init_priority(101)))
#endif


namespace
{
	#pragma region Build Banner

	// Printed before doctest's own banner so a log makes it obvious which of the
	// two builds produced it.
	struct BuildBanner
	{
		BuildBanner()
		{
#if defined(__EMSCRIPTEN__)
			std::printf("[loom] web build (emscripten / wasm)\n");
#elif defined(_WIN32)
			std::printf("[loom] local build (msvc / x64)\n");
#else
			std::printf("[loom] linux build (gcc / x64)\n");
#endif
		};
	} banner;

	#pragma endregion

	#pragma region Turns

#ifdef _WIN32
	// Runs from any slot share Screenshots and the machine, so each takes this in turn.
	constexpr wchar_t SUITE_MUTEX[] = L"Global\\LoomTestSuite";

	constexpr const char* SUITE_EXECUTABLE = "Loom Tests.exe";

	// Calls waiting first when another run holds the suite, so a run waiting its
	// turn does not look hung. False when the wait itself fails.
	bool TakeTurn(HANDLE suite, const std::function<void()>& waiting)
	{
		DWORD taken = WaitForSingleObject(suite, 0);

		if (taken == WAIT_TIMEOUT)
		{
			waiting();
			taken = WaitForSingleObject(suite, INFINITE);
		};

		return taken != WAIT_FAILED;
	};

	int ProcessId()
	{
		return _getpid();
	};

	void SetTempFolder(const std::filesystem::path& folder)
	{
		_wputenv_s(L"TMP", folder.c_str());
		_wputenv_s(L"TEMP", folder.c_str());
	};
#elif !defined(__EMSCRIPTEN__)
	// Outside the temp folder the suite moves into, so every run on the machine finds the same file.
	constexpr const char* SUITE_LOCK = "/tmp/Loom Test Suite.lock";
	constexpr mode_t LOCK_MODE = 0666;

	constexpr const char* SUITE_EXECUTABLE = "Loom Tests";

	int OpenLock(const std::filesystem::path& file)
	{
		return open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, LOCK_MODE);
	};

	// Calls waiting first when another run holds the suite, so a run waiting its
	// turn does not look hung. False when the wait itself fails. Each open of the
	// lock is its own holder, so a process that dies lets go with it.
	bool TakeTurn(int suite, const std::function<void()>& waiting)
	{
		if (flock(suite, LOCK_EX | LOCK_NB) == 0)
			return true;

		if (errno != EWOULDBLOCK)
			return false;

		waiting();

		return flock(suite, LOCK_EX) == 0;
	};

	int ProcessId()
	{
		return getpid();
	};

	void SetTempFolder(const std::filesystem::path& folder)
	{
		setenv("TMPDIR", folder.c_str(), 1);
	};
#endif

#ifndef __EMSCRIPTEN__

	// Before anything below can crash, so nothing in the suite puts up a dialog.
	struct CrashesToStderr
	{
		CrashesToStderr() { Loom::ReportCrashesToStderr(); };
	} crashesToStderr;

	// Tests write fixed names under the temp folder, which other programs on the
	// machine may use too, such as a copy of the suite from before runs took turns.
	struct OwnTempFolder
	{
		const std::filesystem::path folder;

		OwnTempFolder()
			: folder(std::filesystem::temp_directory_path() / ("Loom Tests " + std::to_string(ProcessId())))
		{
			// One left by a crashed run whose process id this one now has.
			std::filesystem::remove_all(folder);
			std::filesystem::create_directory(folder);
			SetTempFolder(folder);
		};

		~OwnTempFolder()
		{
			std::error_code ignored;
			std::filesystem::remove_all(folder, ignored);
		};
	} tempFolder;
#endif

	#pragma endregion

	#pragma region Web Output

#ifdef __EMSCRIPTEN__
	// Emscripten's isatty answers yes for any stdout, so doctest's colours
	// would fill a piped run with escape codes. Node knows.
	EM_JS(bool, StdoutIsTerminal, (), { return !!process.stdout.isTTY; });
#endif

	// Passes what std::cerr is given on to where it went before, a whole line
	// at a time, except the lines STARTUP_CLASH is in, which it counts.
	struct HeldBack final : std::streambuf
	{
		HeldBack() : m_previous(std::cerr.rdbuf(this)) { };

		~HeldBack()
		{
			std::cerr.rdbuf(m_previous);
			m_previous->sputn(m_line.data(), (std::streamsize)m_line.size());
		};

		HeldBack(const HeldBack&) = delete;
		HeldBack& operator=(const HeldBack&) = delete;

		size_t held = 0;

	protected:
		int_type overflow(int_type c) override
		{
			if (traits_type::eq_int_type(c, traits_type::eof()))
				return traits_type::not_eof(c);

			m_line += traits_type::to_char_type(c);

			if (m_line.back() == '\n')
			{
				if (m_line.find(LoomTests::STARTUP_CLASH) != std::string::npos)
					held++;
				else
					m_previous->sputn(m_line.data(), (std::streamsize)m_line.size());

				m_line.clear();
			};

			return c;
		};

	private:
		std::streambuf* m_previous;
		std::string m_line;
	};

	// Held until main counts what it held back.
	std::optional<HeldBack> startup LOOM_FIRST_INITIALISED { std::in_place };

	#pragma endregion

	#pragma region Progress Log

	constexpr std::string_view PROGRESS_LOG_OPTION = "--progress-log=";

	std::ofstream progressFile;

	// Each case's name as it starts and its seconds once it ends, flushed so a stall shows where.
	struct ProgressLog final : doctest::IReporter
	{
		static constexpr int SECONDS_PRECISION = 3;

		explicit ProgressLog(std::ostream& out) : m_out(out) { };
		explicit ProgressLog(const doctest::ContextOptions&) : m_out(progressFile) { };

		void test_case_start(const doctest::TestCaseData& test) override
		{
			m_out << test.m_test_suite << " / " << test.m_name << std::flush;
		};

		void test_case_end(const doctest::CurrentTestCaseStats& stats) override
		{
			m_out << "  " << std::fixed << std::setprecision(SECONDS_PRECISION) << stats.seconds << " s" << std::endl;
		};

		void report_query(const doctest::QueryData&) override { };
		void test_run_start() override { };
		void test_run_end(const doctest::TestRunStats&) override { };
		void test_case_reenter(const doctest::TestCaseData&) override { };
		void test_case_exception(const doctest::TestCaseException&) override { };
		void subcase_start(const doctest::SubcaseSignature&) override { };
		void subcase_end() override { };
		void log_assert(const doctest::AssertData&) override { };
		void log_message(const doctest::MessageData&) override { };
		void test_case_skipped(const doctest::TestCaseData&) override { };

	private:
		std::ostream& m_out;
	};

	REGISTER_LISTENER("progress-log", 0, ProgressLog);

	#pragma endregion
};


int main(int argc, char** argv)
{
#ifndef __EMSCRIPTEN__
	const auto waiting =
		[]()
		{
			std::cerr << "Waiting for another run of the suite to finish" << std::endl;
		};

#ifdef _WIN32
	// Held until the process ends; a run that crashes abandons it, which hands it on all the same.
	const HANDLE suite = CreateMutexW(nullptr, FALSE, SUITE_MUTEX);

	if (!suite || !TakeTurn(suite, waiting))
	{
		std::fprintf(stderr, "The suite could not wait for other runs to finish (error %lu)\n", GetLastError());
		return EXIT_FAILURE;
	};
#else
	// Held until the process ends, when the system closes it.
	const int suite = OpenLock(SUITE_LOCK);

	if (suite < 0 || !TakeTurn(suite, waiting))
	{
		std::fprintf(stderr, "The suite could not wait for other runs to finish (error %d)\n", errno);
		return EXIT_FAILURE;
	};
#endif
#endif

	LoomTests::StartupClashes() = startup->held;
	startup.reset();

	doctest::Context context;

#ifdef __EMSCRIPTEN__
	context.setOption("no-colors", !StdoutIsTerminal());
#endif

	context.applyCommandLine(argc, argv);

	for (int i = 1; i < argc; i++)
	{
		const std::string_view argument = argv[i];

		if (argument.starts_with(PROGRESS_LOG_OPTION))
		{
			const std::string path(argument.substr(PROGRESS_LOG_OPTION.size()));
			progressFile.open(path);

			if (!progressFile)
			{
				std::fprintf(stderr, "The suite could not open its progress log at %s\n", path.c_str());
				return EXIT_FAILURE;
			};
		};
	};

#ifndef __EMSCRIPTEN__
	// The executable's own path, since argv[0] is only what it was launched as.
	const std::filesystem::path executable = Loom::ExecutablePath();

	if (executable.empty())
	{
		std::fprintf(stderr, "The suite could not find its own executable to put screenshots beside\n");
		return EXIT_FAILURE;
	};

	LoomTests::ScreenshotFolder() = executable.parent_path() / "Screenshots";
	std::filesystem::remove_all(LoomTests::ScreenshotFolder());
#endif

	return context.run();
};

// A case outside any TEST_SUITE here would register, under init_seg(lib), before
// doctest.h's TEST_SUITE_END names the default suite, and a -ts= run would crash on it.
TEST_SUITE("Loom Tests")
{
	#pragma region Filters and Progress

	TEST_CASE("every case is in a suite a -ts= filter can pick")
	{
		for (const doctest::detail::TestCase& test : doctest::detail::getRegisteredTests())
		{
			INFO(test.m_file, ":", test.m_line);
			REQUIRE(test.m_test_suite != nullptr);
			CHECK(*test.m_test_suite != '\0');
		};
	};

	TEST_CASE("the progress log names a case as it starts and adds its seconds once it ends")
	{
		constexpr double SECONDS = 1.25;
		const std::string name = "Loom Tests / the progress log names a case as it starts and adds its seconds once it ends";

		std::ostringstream out;
		ProgressLog log(out);

		log.test_case_start(*doctest::getContextOptions()->currentTest);
		CHECK(out.str() == name);

		doctest::CurrentTestCaseStats stats {};
		stats.seconds = SECONDS;
		log.test_case_end(stats);
		CHECK(out.str() == name + "  1.250 s\n");
	};

	#pragma endregion

	#pragma region Runs

#ifndef __EMSCRIPTEN__
	TEST_CASE("the suite's temp files go in a folder of its own")
	{
		CHECK(std::filesystem::equivalent(std::filesystem::temp_directory_path(), tempFolder.folder));
	};

	TEST_CASE("the suite's screenshots go in a folder beside it")
	{
		CHECK(LoomTests::ScreenshotFolder().is_absolute());
		CHECK(LoomTests::ScreenshotFolder().filename() == "Screenshots");
		CHECK(std::filesystem::exists(LoomTests::ScreenshotFolder().parent_path() / SUITE_EXECUTABLE));
	};

#ifdef _WIN32
	TEST_CASE("another run waits while this one runs")
	{
		const HANDLE other = OpenMutexW(SYNCHRONIZE, FALSE, SUITE_MUTEX);
		REQUIRE(other != nullptr);

		DWORD waited = WAIT_FAILED;
		std::thread([&]() { waited = WaitForSingleObject(other, 0); }).join();
		CloseHandle(other);

		CHECK(waited == WAIT_TIMEOUT);
	};

	TEST_CASE("a run takes a free turn without saying it waited")
	{
		const HANDLE turn = CreateMutexW(nullptr, FALSE, nullptr);
		REQUIRE(turn != nullptr);

		bool waited = false;
		CHECK(TakeTurn(turn, [&]() { waited = true; }));
		CHECK_FALSE(waited);

		ReleaseMutex(turn);
		CloseHandle(turn);
	};

	TEST_CASE("a run says it is waiting, then takes its turn once the other run lets go")
	{
		constexpr DWORD LET_GO_TIMEOUT_MS = 5000;

		const HANDLE turn = CreateMutexW(nullptr, FALSE, nullptr);
		const HANDLE letGo = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		REQUIRE(turn != nullptr);
		REQUIRE(letGo != nullptr);

		std::promise<void> held;
		std::thread other(
			[&]()
			{
				WaitForSingleObject(turn, INFINITE);
				held.set_value();
				// Bounded, so a run that never says it waits fails here rather than hanging every run on the machine.
				WaitForSingleObject(letGo, LET_GO_TIMEOUT_MS);
				ReleaseMutex(turn);
			});
		held.get_future().wait();

		bool waited = false;
		CHECK(TakeTurn(turn, [&]() { waited = true; SetEvent(letGo); }));
		CHECK(waited);

		other.join();
		ReleaseMutex(turn);
		CloseHandle(letGo);
		CloseHandle(turn);
	};

#ifdef _DEBUG
	// doctest sets the error mode and abort behaviour itself around each case,
	// but leaves the debug runtime's error reports, which put up abort()'s dialog.
	TEST_CASE("a crash is reported on stderr, with no dialog")
	{
		CHECK(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_REPORT_MODE) == _CRTDBG_MODE_FILE);
	};
#endif
#else
	TEST_CASE("another run waits while this one runs")
	{
		const int other = OpenLock(SUITE_LOCK);
		REQUIRE(other >= 0);

		CHECK(flock(other, LOCK_EX | LOCK_NB) != 0);
		CHECK(errno == EWOULDBLOCK);
		close(other);
	};

	TEST_CASE("a run takes a free turn without saying it waited")
	{
		const int turn = OpenLock(tempFolder.folder / "turn.lock");
		REQUIRE(turn >= 0);

		bool waited = false;
		CHECK(TakeTurn(turn, [&]() { waited = true; }));
		CHECK_FALSE(waited);

		close(turn);
	};

	TEST_CASE("a run says it is waiting, then takes its turn once the other run lets go")
	{
		constexpr std::chrono::seconds LET_GO_TIMEOUT{ 5 };

		const std::filesystem::path file = tempFolder.folder / "turn.lock";
		const int turn = OpenLock(file);
		REQUIRE(turn >= 0);

		std::promise<void> held;
		std::promise<void> letGo;
		std::thread other(
			[&]()
			{
				const int holder = OpenLock(file);
				flock(holder, LOCK_EX);
				held.set_value();
				// Bounded, so a run that never says it waits fails here rather than hanging every run on the machine.
				letGo.get_future().wait_for(LET_GO_TIMEOUT);
				close(holder);
			});
		held.get_future().wait();

		bool waited = false;
		CHECK(TakeTurn(turn, [&]() { waited = true; letGo.set_value(); }));
		CHECK(waited);

		other.join();
		close(turn);
	};
#endif
#endif

	#pragma endregion
};

// Entry point for the Loom test suite.
//
// The same sources are built twice: by MSVC into a desktop executable, and by
// emcc into a WebAssembly module run under node. This is the only translation
// unit that defines doctest's implementation and main(); every other file just
// includes "doctest.h" and adds TEST_CASEs.
//
// Run the executable with no arguments to run everything, or pass
// -ts=<suite> / -tc=<case> to narrow it down. --help lists the rest.

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "Test Support.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <streambuf>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <cstdlib>
#include <process.h>
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
	// Printed before doctest's own banner so a log makes it obvious which of the
	// two builds produced it.
	struct BuildBanner
	{
		BuildBanner()
		{
#ifdef __EMSCRIPTEN__
			std::printf("[loom] web build (emscripten / wasm)\n");
#else
			std::printf("[loom] local build (msvc / x64)\n");
#endif
		};
	} banner;

#ifndef __EMSCRIPTEN__
	// Tests write fixed names under the temp folder, so two suites running at
	// once, from two slots or a merge, would otherwise share each other's files.
	struct OwnTempFolder
	{
		const std::filesystem::path folder;

		OwnTempFolder()
			: folder(std::filesystem::temp_directory_path() / ("Loom Tests " + std::to_string(_getpid())))
		{
			// One left by a crashed run whose process id this one now has.
			std::filesystem::remove_all(folder);
			std::filesystem::create_directory(folder);
			_wputenv_s(L"TMP", folder.c_str());
			_wputenv_s(L"TEMP", folder.c_str());
		};

		~OwnTempFolder()
		{
			std::error_code ignored;
			std::filesystem::remove_all(folder, ignored);
		};
	} tempFolder;
#endif

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
};


int main(int argc, char** argv)
{
	LoomTests::StartupClashes() = startup->held;
	startup.reset();

	doctest::Context context;

#ifdef __EMSCRIPTEN__
	context.setOption("no-colors", !StdoutIsTerminal());
#endif

	context.applyCommandLine(argc, argv);

	return context.run();
};

// A case outside any TEST_SUITE here would register, under init_seg(lib), before
// doctest.h's TEST_SUITE_END names the default suite, and a -ts= run would crash on it.
TEST_SUITE("Loom Tests")
{
	TEST_CASE("every case is in a suite a -ts= filter can pick")
	{
		for (const doctest::detail::TestCase& test : doctest::detail::getRegisteredTests())
		{
			INFO(test.m_file, ":", test.m_line);
			REQUIRE(test.m_test_suite != nullptr);
			CHECK(*test.m_test_suite != '\0');
		};
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE("the suite's temp files go in a folder of its own")
	{
		CHECK(std::filesystem::equivalent(std::filesystem::temp_directory_path(), tempFolder.folder));
	};
#endif
};

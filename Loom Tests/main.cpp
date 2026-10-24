// Entry point for the Loom test suite.
//
// The same sources are built twice: by MSVC into a desktop executable, and by
// emcc into a WebAssembly module run under node. This is the only translation
// unit that defines doctest's implementation and main(); every other file just
// includes "doctest.h" and adds TEST_CASEs.
//
// Run the executable with no arguments to run everything, or pass
// -ts=<suite> / -tc=<case> to narrow it down. --help lists the rest.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <filesystem>
#include <string>

#ifndef __EMSCRIPTEN__
#include <cstdlib>
#include <process.h>
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
};

#ifndef __EMSCRIPTEN__
TEST_CASE("the suite's temp files go in a folder of its own")
{
	CHECK(std::filesystem::equivalent(std::filesystem::temp_directory_path(), tempFolder.folder));
	CHECK(tempFolder.folder.filename().string().ends_with(std::to_string(_getpid())));
};
#endif

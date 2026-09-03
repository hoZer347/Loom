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
};

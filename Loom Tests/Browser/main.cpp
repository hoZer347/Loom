// Entry point for the browser suite.
//
// Its own rather than Loom Tests\main.cpp: the cases here sleep to let the
// browser deliver input, and once main has slept under ASYNCIFY its return
// value no longer reaches Module.onExit. exit() does, and that is how
// run.mjs learns the result.

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdlib>


int main(int argc, char** argv)
{
	std::exit(doctest::Context(argc, argv).run());
};

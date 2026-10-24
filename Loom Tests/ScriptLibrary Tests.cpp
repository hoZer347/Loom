#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "ScriptLibrary.h"


TEST_SUITE("ScriptLibrary")
{
	TEST_CASE("a build line is a warning only when MSBuild says so")
	{
		CHECK(Loom::ScriptLibrary::IsWarning("Laser.hpp(80,82): warning C4244: '=': conversion [Game.vcxproj]"));
		CHECK(!Loom::ScriptLibrary::IsWarning("Laser.hpp(111,21): error C2065: 'broken': undeclared identifier [Game.vcxproj]"));
		CHECK(!Loom::ScriptLibrary::IsWarning("LINK : fatal error LNK1104: cannot open file 'Game.dll'"));
	};
};

#endif

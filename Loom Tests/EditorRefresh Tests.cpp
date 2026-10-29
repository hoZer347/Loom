#include "doctest.h"

#include "EditorRefresh.h"


TEST_SUITE("EditorRefresh")
{
	TEST_CASE("the command builds the editor from the engine's sources")
	{
#ifdef _WIN32
		const std::string command = Loom::EditorRefresh::Command("C:\\VS\\MSBuild.exe", "C:\\Loom\\Loom.sln", "Release");

		CHECK(command.starts_with("\"C:\\VS\\MSBuild.exe\" \"C:\\Loom\\Loom.sln\" \"-t:Loom Editor\""));
		CHECK(command.find(" -p:Configuration=Release ") != std::string::npos);
#else
		CHECK(Loom::EditorRefresh::Command("/usr/bin/cmake", "/home/me/Loom/build/CMakeCache.txt", "Release") ==
			"/usr/bin/cmake --build \"/home/me/Loom/build\" --target LoomEditor");
#endif
	};
};

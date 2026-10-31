#include "doctest.h"

// The web test build has no source tree to read and no precompiled header.
#ifndef __EMSCRIPTEN__

#include <filesystem>
#include <fstream>
#include <string>


namespace
{
	const std::filesystem::path PRECOMPILED = std::filesystem::path(__FILE__).parent_path().parent_path() / "Precompiled";

	// A quoted include is one of the repository's own headers, which would rebuild every file when edited.
	bool IncludesOnlySystemHeaders(const std::filesystem::path& header, const std::string& allowed)
	{
		const std::string QUOTED = "#include \"";
		std::ifstream file(header);
		REQUIRE(file.good());
		std::string line;
		while (std::getline(file, line))
		{
			if (line.rfind(QUOTED, 0) == 0 && line.find(allowed) == std::string::npos)
				return false;
		};
		return true;
	};
};

TEST_SUITE("Precompiled")
{
	TEST_CASE("precompiled headers hold no header of the engine")
	{
		const std::string COMMON = "\"Common.h\"";
		CHECK(IncludesOnlySystemHeaders(PRECOMPILED / "Common.h", COMMON));
		CHECK(IncludesOnlySystemHeaders(PRECOMPILED / "Tests.h", COMMON));
	};
};

#endif

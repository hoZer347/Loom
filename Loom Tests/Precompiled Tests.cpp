#include "doctest.h"

#ifndef __EMSCRIPTEN__

#include <filesystem>
#include <fstream>
#include <string>


namespace
{
	const std::filesystem::path ROOT = std::filesystem::path(__FILE__).parent_path().parent_path();
	const std::filesystem::path PRECOMPILED = ROOT / "Precompiled";

	std::string IncludedName(const std::string& line)
	{
		const std::string DIRECTIVE = "#include";
		const size_t directive = line.find_first_not_of(" \t");
		if (directive == std::string::npos || line.compare(directive, DIRECTIVE.size(), DIRECTIVE) != 0)
			return "";
		const size_t open = line.find_first_of("\"<", directive);
		if (open == std::string::npos)
			return "";
		const size_t close = line.find_first_of("\">", open + 1);
		return close == std::string::npos ? "" : line.substr(open + 1, close - open - 1);
	};

	bool IsEngineHeader(const std::string& name)
	{
		for (const char* folder : { "Loom WASM", "Loom Editor", "Loom Tests" })
		{
			if (std::filesystem::exists(ROOT / folder / name))
				return true;
		};
		return false;
	};
};

TEST_SUITE("Precompiled")
{
	TEST_CASE("precompiled headers hold no header of the engine")
	{
		size_t headers = 0;
		for (const auto& entry : std::filesystem::directory_iterator(PRECOMPILED))
		{
			if (entry.path().extension() != ".h")
				continue;
			++headers;
			CAPTURE(entry.path().string());
			std::ifstream file(entry.path());
			std::string line;
			while (std::getline(file, line))
			{
				const std::string name = IncludedName(line);
				CAPTURE(name);
				if (!name.empty() && !std::filesystem::exists(PRECOMPILED / name))
					CHECK_FALSE(IsEngineHeader(name));
			};
		};
		CHECK(headers > 0);
	};
};

#endif

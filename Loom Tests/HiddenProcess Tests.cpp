#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "HiddenProcess.h"

#include <string>
#include <vector>


namespace
{
	std::vector<std::string> Lines(const std::string& command_line, bool* succeeded)
	{
		std::vector<std::string> lines;

		*succeeded = Loom::HiddenProcess::Run(
			command_line,
			[&](const std::string& line) { lines.push_back(line); });

		return lines;
	};
};

TEST_SUITE("HiddenProcess")
{
	TEST_CASE("lines come without their CRLF, an empty one kept and a last one with no newline included")
	{
		bool succeeded = true;

		const std::vector<std::string> lines =
			Lines("cmd.exe /c \"echo first& echo.& <nul set /p =last& exit 0\"", &succeeded);

		CHECK(succeeded);
		CHECK(lines == std::vector<std::string>{ "first", "", "last" });
	};

	TEST_CASE("stderr comes down the same pipe")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines("cmd.exe /c \"echo oops 1>&2\"", &succeeded);

		CHECK(lines == std::vector<std::string>{ "oops " });
	};

	TEST_CASE("a non-zero exit is a failure, its output still read")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines("cmd.exe /c \"echo said& exit 3\"", &succeeded);

		CHECK_FALSE(succeeded);
		CHECK(lines == std::vector<std::string>{ "said" });
	};

	TEST_CASE("a program that is not there is a failure with no lines")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines("\"C:\\no such folder\\nothing.exe\"", &succeeded);

		CHECK_FALSE(succeeded);
		CHECK(lines.empty());
	};
};

#endif

#include "doctest.h"

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "HiddenProcess.h"

#include <string>
#include <vector>


namespace
{
#ifdef _WIN32
	constexpr const char* THREE_LINES = "cmd.exe /c \"echo first& echo.& <nul set /p =last& exit 0\"";
	constexpr const char* TO_STDERR = "cmd.exe /c \"echo oops 1>&2\"";
	constexpr const char* FAILING = "cmd.exe /c \"echo said& exit 3\"";
	constexpr const char* MISSING = "\"C:\\no such folder\\nothing.exe\"";

	// Windows fails to start it, where the shell starts and says it is not there.
	constexpr size_t MISSING_LINES = 0;
#else
	constexpr const char* THREE_LINES = "printf 'first\\r\\n\\r\\nlast'; exit 0";
	constexpr const char* TO_STDERR = "echo 'oops ' 1>&2";
	constexpr const char* FAILING = "echo said; exit 3";
	constexpr const char* MISSING = "'/no such folder/nothing'";

	constexpr size_t MISSING_LINES = 1;
#endif

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

		const std::vector<std::string> lines = Lines(THREE_LINES, &succeeded);

		CHECK(succeeded);
		CHECK(lines == std::vector<std::string>{ "first", "", "last" });
	};

	TEST_CASE("stderr comes down the same pipe")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines(TO_STDERR, &succeeded);

		CHECK(lines == std::vector<std::string>{ "oops " });
	};

	TEST_CASE("a non-zero exit is a failure, its output still read")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines(FAILING, &succeeded);

		CHECK_FALSE(succeeded);
		CHECK(lines == std::vector<std::string>{ "said" });
	};

	TEST_CASE("a program that is not there is a failure with no lines but the system's own")
	{
		bool succeeded = true;

		const std::vector<std::string> lines = Lines(MISSING, &succeeded);

		CHECK_FALSE(succeeded);
		CHECK(lines.size() == MISSING_LINES);
	};
};

#endif

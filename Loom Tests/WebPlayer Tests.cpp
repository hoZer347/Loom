#include "doctest.h"

#include "WebPlayer.h"

#include <string>
#include <vector>


TEST_SUITE("WebPlayer")
{
	TEST_CASE("the listening line gives the page on localhost at the port emrun took")
	{
		CHECK(
			Loom::WebPlayer::PageUrl("Now listening at http://127.0.0.1:53125/", "/Build/Web/index.html") ==
			"http://localhost:53125/Build/Web/index.html");
	};

	TEST_CASE("any other line gives nothing")
	{
		CHECK(Loom::WebPlayer::PageUrl("Linking 1 script file(s)", "/index.html").empty());
		CHECK(Loom::WebPlayer::PageUrl("", "/index.html").empty());
	};

	TEST_CASE("a listening line without a port gives nothing")
	{
		CHECK(Loom::WebPlayer::PageUrl("Now listening at http://127.0.0.1/", "/index.html").empty());
		CHECK(Loom::WebPlayer::PageUrl("Now listening at http://localhost", "/index.html").empty());
	};

	TEST_CASE("a script that serves the page gives its address once and leaves nothing to print")
	{
		Loom::WebPlayer::Output output("/index.html");

		CHECK(output.Hear("Linking 1 script file(s)").empty());
		CHECK(output.Hear("Now listening at http://127.0.0.1:53125/") == "http://localhost:53125/index.html");
		CHECK(output.Hear("Now listening at http://127.0.0.1:53126/").empty());
		CHECK(output.Ended(false).empty());
	};

	TEST_CASE("the page counts as served from the line saying so, and not before")
	{
		Loom::WebPlayer::Output output("/index.html");

		output.Hear("Linking 1 script file(s)");
		CHECK_FALSE(output.Serving());

		output.Hear("Now listening at http://127.0.0.1:53125/");
		CHECK(output.Serving());

		output.Hear("Sending the page");
		CHECK(output.Serving());
	};

	TEST_CASE("a script that ends before serving leaves everything it said to print")
	{
		Loom::WebPlayer::Output output("/index.html");

		output.Hear("Linking 1 script file(s)");
		output.Hear("");
		output.Hear("The scripts failed to build.");

		CHECK(output.Ended(false) == std::vector<std::string>{ "Linking 1 script file(s)", "The scripts failed to build." });
	};

	TEST_CASE("a script stopped before serving leaves nothing to print")
	{
		Loom::WebPlayer::Output output("/index.html");

		output.Hear("Linking 1 script file(s)");

		CHECK(output.Ended(true).empty());
	};
};

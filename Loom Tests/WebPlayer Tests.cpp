#include "doctest.h"

#include "WebPlayer.h"

#include <string>


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
};

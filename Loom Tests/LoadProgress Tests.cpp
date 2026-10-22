#include "doctest.h"

#include "LoadProgress.h"

#include <sstream>


TEST_SUITE("LoadProgress")
{
	Loom::BuildTimings ReadAll(const std::string& text)
	{
		std::istringstream in(text);
		return Loom::BuildTimings::Read(in);
	};

	TEST_CASE("what is written reads back the same")
	{
		Loom::BuildTimings written;
		written.Set(false, 2.5);
		written.Set(true, 11.75);

		std::ostringstream out;
		written.Write(out);

		const Loom::BuildTimings read = ReadAll(out.str());

		CHECK(read.Of(false) == doctest::Approx(2.5));
		CHECK(read.Of(true) == doctest::Approx(11.75));
	};

	TEST_CASE("one kind set leaves the other unknown")
	{
		Loom::BuildTimings written;
		written.Set(true, 12.0);

		std::ostringstream out;
		written.Write(out);

		const Loom::BuildTimings read = ReadAll(out.str());

		CHECK_FALSE(read.Of(false).has_value());
		CHECK(read.Of(true) == doctest::Approx(12.0));
	};

	TEST_CASE("nothing to read, or nothing it understands, gives no history")
	{
		CHECK_FALSE(ReadAll("").Of(false).has_value());
		CHECK_FALSE(ReadAll("").Of(true).has_value());

		const Loom::BuildTimings read = ReadAll("build\nrebuild=soon\nlink=3\nbuild=-4\nrebuild=0\nbuild=5x\n");

		CHECK_FALSE(read.Of(false).has_value());
		CHECK_FALSE(read.Of(true).has_value());
	};

	TEST_CASE("a file written on Windows reads the same")
	{
		CHECK(ReadAll("build=3\r\nrebuild=9\r\n").Of(true) == doctest::Approx(9.0));
	};

	TEST_CASE("a build fills by its time against the last of its kind")
	{
		CHECK(Loom::BuildTimings::Progress(0.0, 10.0) == doctest::Approx(0.0f));
		CHECK(Loom::BuildTimings::Progress(2.5, 10.0) == doctest::Approx(0.25f));
	};

	TEST_CASE("a build that outlasts the last holds short of the end")
	{
		const float at_estimate = *Loom::BuildTimings::Progress(10.0, 10.0);
		const float long_after = *Loom::BuildTimings::Progress(100.0, 10.0);

		CHECK(at_estimate < 1.0f);
		CHECK(long_after == doctest::Approx(at_estimate));
	};

	TEST_CASE("a build with no history has no progress")
	{
		CHECK_FALSE(Loom::BuildTimings::Progress(3.0, std::nullopt).has_value());
		CHECK_FALSE(Loom::BuildTimings::Progress(3.0, 0.0).has_value());
	};

	TEST_CASE("building and then opening fill a half each")
	{
		const Loom::ProjectLoad load{ .compiles = true, .opens = true };

		CHECK(load.Fraction(false, 0.0f) == doctest::Approx(0.0f));
		CHECK(load.Fraction(false, 0.5f) == doctest::Approx(0.25f));
		CHECK(load.Fraction(true, 0.0f) == doctest::Approx(0.5f));
		CHECK(load.Fraction(true, 1.0f) == doctest::Approx(1.0f));
	};

	TEST_CASE("a step on its own fills the whole bar")
	{
		const Loom::ProjectLoad building{ .compiles = true };
		const Loom::ProjectLoad opening{ .opens = true };

		CHECK(building.Fraction(false, 0.5f) == doctest::Approx(0.5f));
		CHECK(opening.Fraction(true, 0.5f) == doctest::Approx(0.5f));
	};

	// New Project opens its scene once the build is done, though nothing
	// asked it to open one.
	TEST_CASE("a scene opened after a build that expected none takes the second half")
	{
		const Loom::ProjectLoad load{ .compiles = true };

		CHECK(load.Fraction(true, 0.0f) == doctest::Approx(0.5f));
		CHECK(load.Fraction(true, 1.0f) == doctest::Approx(1.0f));
	};
};

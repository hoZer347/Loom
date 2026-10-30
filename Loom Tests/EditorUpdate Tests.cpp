#include "doctest.h"

#include "EditorUpdate.h"


using Kind = Loom::EditorUpdate::Kind;

TEST_SUITE("EditorUpdate")
{
	TEST_CASE("nothing is due until something is asked for")
	{
		Loom::EditorUpdate update;

		CHECK(update.Waiting() == Kind::None);
		CHECK(update.Due(false) == Kind::None);
	};

	TEST_CASE("an update waits only for the busy editor")
	{
		Loom::EditorUpdate update;
		update.Request(Kind::Restart);

		CHECK(update.Due(true) == Kind::None);
		CHECK(update.Waiting() == Kind::Restart);

		CHECK(update.Due(false) == Kind::Restart);
		CHECK(update.Waiting() == Kind::None);
	};

	TEST_CASE("the scripts wait for the busy editor the same way")
	{
		Loom::EditorUpdate update;
		update.Request(Kind::Scripts);

		CHECK(update.Due(true) == Kind::None);
		CHECK(update.Due(false) == Kind::Scripts);
	};

	TEST_CASE("a restart asked for outranks the scripts, whichever came first")
	{
		Loom::EditorUpdate update;
		update.Request(Kind::Scripts);
		update.Request(Kind::Restart);
		update.Request(Kind::Scripts);

		CHECK(update.Waiting() == Kind::Restart);
	};
};

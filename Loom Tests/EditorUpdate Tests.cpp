#include "doctest.h"

#include "EditorUpdate.h"


using Kind = Loom::EditorUpdate::Kind;

TEST_SUITE("EditorUpdate")
{
	TEST_CASE("nothing is due until something is asked for")
	{
		Loom::EditorUpdate update;

		CHECK(update.Waiting() == Kind::None);
		CHECK(update.Due(false, false, false) == Kind::None);
	};

	TEST_CASE("a restart waits for the run, the busy editor and anything it would lose")
	{
		Loom::EditorUpdate update;
		update.Request(Kind::Restart);

		CHECK(update.Due(true, false, false) == Kind::None);
		CHECK(update.Due(false, true, false) == Kind::None);
		CHECK(update.Due(false, false, true) == Kind::None);
		CHECK(update.Waiting() == Kind::Restart);

		CHECK(update.Due(false, false, false) == Kind::Restart);
		CHECK(update.Waiting() == Kind::None);
	};

	TEST_CASE("the scripts build over unsaved work, which their compile keeps")
	{
		Loom::EditorUpdate update;
		update.Request(Kind::Scripts);

		CHECK(update.Due(true, false, true) == Kind::None);
		CHECK(update.Due(false, true, true) == Kind::None);
		CHECK(update.Due(false, false, true) == Kind::Scripts);
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

#include "doctest.h"

#include "EditHistory.h"

#include <string>


TEST_SUITE("EditHistory")
{
	TEST_CASE("a step to undo or redo is there exactly when one was kept or taken back")
	{
		Loom::EditHistory<std::string> history;
		std::string value = "saved";

		history.Reset(value);
		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());

		history.Keep("edited");
		CHECK(history.CanUndo());
		CHECK_FALSE(history.CanRedo());

		REQUIRE(history.Step(value, true));
		CHECK(value == "saved");
		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());
	};

	TEST_CASE("a value amended after a step is no step of its own, and redo still follows")
	{
		Loom::EditHistory<std::string> history;
		std::string value = "saved";

		history.Reset(value);
		history.Keep("edited");

		REQUIRE(history.Step(value, true));

		// As the Prefab panel's copy writes itself once a step has shown it.
		history.Amend("saved again");
		history.Keep("saved again");

		CHECK(history.CanRedo());
		REQUIRE(history.Step(value, false));
		CHECK(value == "edited");
		REQUIRE(history.Step(value, true));
		CHECK(value == "saved again");
	};
};

#include "doctest.h"

#include "UndoHistory.h"

#include <string>


TEST_SUITE("UndoHistory")
{
	TEST_CASE("a step undoes to the copy pushed before it, and redoes back")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "edited";

		history.Push("first");
		REQUIRE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());

		REQUIRE(history.Step(value, true));
		CHECK(value == "first");
		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());

		REQUIRE(history.Step(value, false));
		CHECK(value == "edited");
		CHECK_FALSE(history.Step(value, false));
		CHECK(value == "edited");
	};

	TEST_CASE("undoing back to the save marks it saved, and redoing past marks it unsaved")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "saved";

		CHECK_FALSE(history.Unsaved());

		history.Push(value);
		value = "edited";
		CHECK(history.Unsaved());

		REQUIRE(history.Step(value, true));
		CHECK_FALSE(history.Unsaved());

		REQUIRE(history.Step(value, false));
		CHECK(history.Unsaved());

		history.Saved();
		CHECK_FALSE(history.Unsaved());

		REQUIRE(history.Step(value, true));
		CHECK(history.Unsaved());
	};

	TEST_CASE("a new step after undoing past the save leaves the save out of reach")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "first";

		history.Push(value);
		value = "saved";
		history.Saved();

		REQUIRE(history.Step(value, true));
		history.Push(value);
		value = "other";
		CHECK_FALSE(history.CanRedo());

		REQUIRE(history.Step(value, true));
		CHECK(value == "first");
		CHECK(history.Unsaved());
		CHECK_FALSE(history.Step(value, true));
		CHECK(history.Unsaved());
	};

	TEST_CASE("only the newest steps up to the limit are kept")
	{
		constexpr size_t limit = 3;
		constexpr size_t pushed = limit + 2;

		Loom::UndoHistory<size_t> history;
		size_t value = pushed;

		for (size_t step = 0; step < pushed; step++)
			history.Push(step, limit);

		size_t undone = 0;

		while (history.Step(value, true))
			undone++;

		CHECK(undone == limit);
		CHECK(value == pushed - limit);
		CHECK(history.Unsaved());
	};

	TEST_CASE("Pop drops the newest step to undo")
	{
		Loom::UndoHistory<std::string> history;

		CHECK(history.Last() == nullptr);
		history.Pop();

		history.Push("first");
		history.Push("second");
		REQUIRE(history.Last() != nullptr);
		CHECK(*history.Last() == "second");

		history.Pop();
		CHECK(*history.Last() == "first");

		history.Pop();
		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.Unsaved());
	};

	TEST_CASE("Next names what a step would hand back without taking it")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "edited";

		CHECK(history.Next(true) == nullptr);
		CHECK(history.Next(false) == nullptr);

		history.Push("first");
		REQUIRE(history.Next(true) != nullptr);
		CHECK(*history.Next(true) == "first");
		CHECK(history.CanUndo());

		REQUIRE(history.Step(value, true));
		REQUIRE(history.Next(false) != nullptr);
		CHECK(*history.Next(false) == "edited");
	};

	TEST_CASE("Rewrite carries a change into every step, either way")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "c";

		history.Push("a");
		history.Push("b");
		REQUIRE(history.Step(value, true));

		history.Rewrite([](const std::string& step) { return step + "!"; });

		REQUIRE(history.Step(value, true));
		CHECK(value == "a!");

		REQUIRE(history.Step(value, false));
		REQUIRE(history.Step(value, false));
		CHECK(value == "c!");
	};

	TEST_CASE("Clear leaves nothing to step to, and what stands as saved")
	{
		Loom::UndoHistory<std::string> history;
		std::string value = "edited";

		history.Push("first");
		REQUIRE(history.Step(value, true));
		history.Clear();

		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK_FALSE(history.Unsaved());
	};
};

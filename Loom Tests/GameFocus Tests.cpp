#include "doctest.h"

#include "GameFocus.h"


TEST_SUITE("GameFocus")
{
	struct Frame
	{
		bool panel_focused = false;
		bool panel_hovered = false;
		bool button_went_down = false;
		bool button_held = false;
	};

	bool Run(Loom::GameFocus& focus, const Frame& frame)
	{
		return focus.Update(frame.panel_focused, frame.panel_hovered, frame.button_went_down, frame.button_held);
	};

	TEST_CASE("the focused panel hears nothing pressed, and an unfocused one does not")
	{
		Loom::GameFocus focus;

		CHECK(Run(focus, { .panel_focused = true }));
		CHECK_FALSE(Run(focus, { .panel_hovered = true }));
	};

	// ImGui still has the Game panel focused in the frame a click lands on
	// another panel, and never moves focus at all for a right click.
	TEST_CASE("a press on another panel is not heard while it is held, though Game keeps focus")
	{
		Loom::GameFocus focus;

		CHECK_FALSE(Run(focus, { .panel_focused = true, .button_went_down = true, .button_held = true }));
		CHECK_FALSE(Run(focus, { .panel_focused = true, .button_held = true }));
		CHECK(Run(focus, { .panel_focused = true }));
	};

	TEST_CASE("the press that brings focus is heard before ImGui moves it")
	{
		Loom::GameFocus focus;

		CHECK(Run(focus, { .panel_hovered = true, .button_went_down = true, .button_held = true }));
		CHECK(Run(focus, { .panel_focused = true, .panel_hovered = true, .button_held = true }));
	};

	TEST_CASE("a press that leaves the panel while held stays the game's")
	{
		Loom::GameFocus focus;

		CHECK(Run(focus, { .panel_focused = true, .panel_hovered = true, .button_went_down = true, .button_held = true }));
		CHECK(Run(focus, { .panel_focused = true, .button_held = true }));
	};

	// The release frame passes the button as held, which keeps the press's
	// decision for the Up that ends it.
	TEST_CASE("the release of a press elsewhere is not heard, though Game is focused")
	{
		Loom::GameFocus focus;

		CHECK_FALSE(Run(focus, { .panel_focused = true, .button_went_down = true, .button_held = true }));
		CHECK_FALSE(Run(focus, { .panel_focused = true, .button_held = true }));
	};

	TEST_CASE("the release of a press on an unfocused Game is heard")
	{
		Loom::GameFocus focus;

		CHECK(Run(focus, { .panel_hovered = true, .button_went_down = true, .button_held = true }));
		CHECK(Run(focus, { .button_held = true }));
		CHECK_FALSE(Run(focus, { }));
	};

	TEST_CASE("a hidden panel hears nothing")
	{
		Loom::GameFocus focus;

		CHECK_FALSE(Run(focus, { .button_went_down = true, .button_held = true }));
		CHECK_FALSE(Run(focus, { }));
	};
};

#include "doctest.h"

#include "Input.h"

#include <type_traits>


TEST_SUITE("Input")
{
	// GLFW's numbering, which Input keeps.
	constexpr int left_button = 0;
	constexpr int right_button = 1;
	constexpr int middle_button = 2;

	// Input is a namespace-with-state, not something a scene owns: Engine wires
	// the platform callbacks up once and everything reads the statics.
	TEST_CASE("Input cannot be instantiated")
	{
		CHECK_FALSE(std::is_constructible_v<Loom::Input>);
		CHECK_FALSE(std::is_destructible_v<Loom::Input>);
	};

	// Lets go of every button and runs the frame out, so a case starts from
	// nothing held and no edges pending.
	void ReleaseAll()
	{
		for (int button = 0; button < Loom::Input::mouse_buttons; button++)
			Loom::Input::OnMouseButton(button, false);

		Loom::Input::Tick();
		Loom::Input::Tick();
	};

	TEST_CASE("a press is down for the frame it lands in, then held")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseButtonDown(left_button));
		CHECK(Loom::Input::GetMouseButton(left_button));
		CHECK_FALSE(Loom::Input::GetMouseButtonUp(left_button));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButtonDown(left_button));
		CHECK(Loom::Input::GetMouseButton(left_button));

		ReleaseAll();
	};

	TEST_CASE("a release is up for one frame and the button is no longer held")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(right_button, true);
		Loom::Input::Tick();
		Loom::Input::OnMouseButton(right_button, false);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseButtonUp(right_button));
		CHECK_FALSE(Loom::Input::GetMouseButton(right_button));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButtonUp(right_button));
	};

	// Down and up between two polls: a frame that asked only whether the
	// button is held would never see the click at all.
	TEST_CASE("a click inside one frame still reads as down, held and up")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(middle_button, true);
		Loom::Input::OnMouseButton(middle_button, false);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseButtonDown(middle_button));
		CHECK(Loom::Input::GetMouseButton(middle_button));
		CHECK(Loom::Input::GetMouseButtonUp(middle_button));

		ReleaseAll();
	};

	TEST_CASE("nothing waits on a Tick before it is reported")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(left_button, true);

		CHECK_FALSE(Loom::Input::GetMouseButtonDown(left_button));

		ReleaseAll();
	};

	TEST_CASE("buttons are separate, and ones GLFW does not have are never pressed")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::OnMouseButton(-1, true);
		Loom::Input::OnMouseButton(Loom::Input::mouse_buttons, true);
		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButton(right_button));
		CHECK_FALSE(Loom::Input::GetMouseButton(-1));
		CHECK_FALSE(Loom::Input::GetMouseButtonDown(Loom::Input::mouse_buttons));

		ReleaseAll();
	};

	// The cursor callback writes these; Engine and any scene reads them. They
	// have to be plain settable ints for the callback to be able to.
	TEST_CASE("the cursor and screen fields round-trip")
	{
		const int screenWidth  = Loom::Input::screen_width;
		const int screenHeight = Loom::Input::screen_height;
		const int mouseX       = Loom::Input::mouse_x;
		const int mouseY       = Loom::Input::mouse_y;

		Loom::Input::screen_width  = 1280;
		Loom::Input::screen_height = 720;
		Loom::Input::mouse_x       = 640;
		Loom::Input::mouse_y       = 360;

		CHECK(Loom::Input::screen_width  == 1280);
		CHECK(Loom::Input::screen_height == 720);
		CHECK(Loom::Input::mouse_x       == 640);
		CHECK(Loom::Input::mouse_y       == 360);

		Loom::Input::screen_width  = screenWidth;
		Loom::Input::screen_height = screenHeight;
		Loom::Input::mouse_x       = mouseX;
		Loom::Input::mouse_y       = mouseY;
	};
};

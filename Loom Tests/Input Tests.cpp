#include "doctest.h"

#include "Input.h"

#include <type_traits>


TEST_SUITE("Input")
{
	// Input is a namespace-with-state, not something a scene owns: Engine wires
	// the platform callbacks up once and everything reads the statics.
	TEST_CASE("Input cannot be instantiated")
	{
		CHECK_FALSE(std::is_constructible_v<Loom::Input>);
		CHECK_FALSE(std::is_destructible_v<Loom::Input>);
	};

	// The button queries are declared but not wired to GLFW or to the browser
	// yet. Pinning the current answer down means the day they start reporting
	// real state, this test is what says so.
	TEST_CASE("the mouse button queries are still stubs and always report not-pressed")
	{
		for (int button = 0; button < 8; button++)
		{
			CHECK_FALSE(Loom::Input::GetMouseButtonDown(button));
			CHECK_FALSE(Loom::Input::GetMouseButton(button));
			CHECK_FALSE(Loom::Input::GetMouseButtonUp(button));
		};
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

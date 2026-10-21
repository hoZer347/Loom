#include "doctest.h"

#include "Input.h"

#include <type_traits>


TEST_SUITE("Input")
{
	// GLFW's numbering, which Input keeps.
	constexpr int left_button = 0;
	constexpr int right_button = 1;
	constexpr int middle_button = 2;
	constexpr int w_key = (int)Loom::Key::W;
	constexpr int left_shift = (int)Loom::Key::LeftShift;

	// Input is a namespace-with-state, not something a scene owns: Engine wires
	// the platform callbacks up once and everything reads the statics.
	TEST_CASE("Input cannot be instantiated")
	{
		CHECK_FALSE(std::is_constructible_v<Loom::Input>);
		CHECK_FALSE(std::is_destructible_v<Loom::Input>);
	};

	// Lets go of every button and key and runs the frame out, so a case
	// starts from nothing held, no edges pending and the wheel still.
	void ReleaseAll()
	{
		for (int button = 0; button < Loom::Input::mouse_buttons; button++)
			Loom::Input::OnMouseButton(button, false);

		for (int key = 0; key < Loom::Input::keys; key++)
			Loom::Input::OnKey(key, false);

		Loom::Input::Tick();
		Loom::Input::Tick();
	};

	// The cursor on the whole of a window, which is the view.
	void CursorInWindow()
	{
		constexpr int window_width = 1280;
		constexpr int window_height = 720;
		constexpr int cursor_x = 300;
		constexpr int cursor_y = 200;

		Loom::Input::ClearView();
		Loom::Input::OnMouseMove(cursor_x, cursor_y, window_width, window_height);
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

	// An editor's other panels take clicks the game must not see.
	TEST_CASE("without focus nothing is down or held, and a held button comes up once")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::Tick();
		Loom::Input::Tick();
		Loom::Input::SetFocused(false);

		CHECK_FALSE(Loom::Input::IsFocused());
		CHECK_FALSE(Loom::Input::GetMouseButtonDown(left_button));
		CHECK_FALSE(Loom::Input::GetMouseButton(left_button));
		CHECK(Loom::Input::GetMouseButtonUp(left_button));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButtonUp(left_button));

		// A click made elsewhere is neither down nor up for the game.
		Loom::Input::OnMouseButton(right_button, true);
		Loom::Input::OnMouseButton(right_button, false);
		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButtonDown(right_button));
		CHECK_FALSE(Loom::Input::GetMouseButtonUp(right_button));

		Loom::Input::SetFocused(true);
		ReleaseAll();
	};

	TEST_CASE("the press that takes focus away is not heard to come up")
	{
		ReleaseAll();

		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::Tick();
		Loom::Input::SetFocused(false);

		CHECK_FALSE(Loom::Input::GetMouseButtonUp(left_button));

		Loom::Input::SetFocused(true);
		ReleaseAll();
	};

	TEST_CASE("focus coming back hears the press that brought it")
	{
		ReleaseAll();
		Loom::Input::SetFocused(false);

		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::Tick();
		Loom::Input::SetFocused(true);

		CHECK(Loom::Input::GetMouseButtonDown(left_button));
		CHECK(Loom::Input::GetMouseButton(left_button));

		ReleaseAll();
	};

	TEST_CASE("a press made up after Tick is heard in the frame it is made")
	{
		ReleaseAll();

		Loom::Input::OnMouseButtonThisFrame(left_button, true);

		CHECK(Loom::Input::GetMouseButtonDown(left_button));
		CHECK(Loom::Input::GetMouseButton(left_button));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetMouseButtonDown(left_button));
		CHECK(Loom::Input::GetMouseButton(left_button));

		Loom::Input::OnMouseButtonThisFrame(left_button, false);

		CHECK(Loom::Input::GetMouseButtonUp(left_button));
		CHECK_FALSE(Loom::Input::GetMouseButton(left_button));

		ReleaseAll();
	};

	TEST_CASE("a key is down for the frame it lands in, then held, then up for one frame")
	{
		ReleaseAll();

		Loom::Input::OnKey(w_key, true);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetKeyDown(w_key));
		CHECK(Loom::Input::GetKey(w_key));
		CHECK_FALSE(Loom::Input::GetKey(left_shift));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetKeyDown(w_key));
		CHECK(Loom::Input::GetKey(w_key));

		Loom::Input::OnKey(w_key, false);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetKeyUp(w_key));
		CHECK_FALSE(Loom::Input::GetKey(w_key));

		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetKeyUp(w_key));
	};

	TEST_CASE("keys GLFW does not have are never pressed, and a button is not a key")
	{
		ReleaseAll();

		Loom::Input::OnKey(-1, true);
		Loom::Input::OnKey(Loom::Input::keys, true);
		Loom::Input::OnMouseButton(left_button, true);
		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetKey(-1));
		CHECK_FALSE(Loom::Input::GetKeyDown(Loom::Input::keys));
		CHECK_FALSE(Loom::Input::GetKey(left_button));

		ReleaseAll();
	};

	TEST_CASE("without focus no key is heard, and a held key comes up once")
	{
		ReleaseAll();

		Loom::Input::OnKey(left_shift, true);
		Loom::Input::Tick();
		Loom::Input::Tick();
		Loom::Input::SetFocused(false);

		CHECK_FALSE(Loom::Input::GetKey(left_shift));
		CHECK(Loom::Input::GetKeyUp(left_shift));

		Loom::Input::OnKey(w_key, true);
		Loom::Input::Tick();

		CHECK_FALSE(Loom::Input::GetKeyUp(left_shift));
		CHECK_FALSE(Loom::Input::GetKeyDown(w_key));

		Loom::Input::SetFocused(true);
		ReleaseAll();
	};

	TEST_CASE("a key pressed after Tick is heard in the frame it is pressed")
	{
		ReleaseAll();

		Loom::Input::OnKeyThisFrame(w_key, true);

		CHECK(Loom::Input::GetKeyDown(w_key));
		CHECK(Loom::Input::GetKey(w_key));

		Loom::Input::Tick();
		Loom::Input::OnKeyThisFrame(w_key, false);

		CHECK(Loom::Input::GetKeyUp(w_key));
		CHECK_FALSE(Loom::Input::GetKey(w_key));

		ReleaseAll();
	};

	TEST_CASE("the wheel's notches add up over a frame and are heard for that frame alone")
	{
		constexpr float first = 1.0f;
		constexpr float second = 0.5f;

		ReleaseAll();
		CursorInWindow();

		Loom::Input::OnMouseWheel(first);

		CHECK(Loom::Input::GetMouseWheel() == 0.0f);

		Loom::Input::OnMouseWheel(second);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseWheel() == first + second);

		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseWheel() == 0.0f);

		Loom::Input::OnMouseWheelThisFrame(-first);

		CHECK(Loom::Input::GetMouseWheel() == -first);

		ReleaseAll();
	};

	// An editor's other panels scroll with it.
	TEST_CASE("the wheel turns nothing without focus or with the cursor off the view")
	{
		constexpr float notches = 1.0f;

		ReleaseAll();
		CursorInWindow();

		Loom::Input::OnMouseWheel(notches);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetMouseWheel() == notches);

		Loom::Input::SetFocused(false);

		CHECK(Loom::Input::GetMouseWheel() == 0.0f);

		Loom::Input::SetFocused(true);

		// Just past the window's far corner.
		const int width = Loom::Input::screen_width;
		const int height = Loom::Input::screen_height;
		Loom::Input::OnMouseMove(width, height, width, height);

		CHECK(Loom::Input::GetMouseWheel() == 0.0f);

		// A Game view that is not drawn.
		CursorInWindow();
		Loom::Input::SetView(0, 0, 0, 0);

		CHECK(Loom::Input::GetMouseWheel() == 0.0f);

		Loom::Input::ClearView();
		ReleaseAll();
	};

	TEST_CASE("a key asked by name is the key asked by number")
	{
		ReleaseAll();

		Loom::Input::OnKey(w_key, true);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetKeyDown(Loom::Key::W));
		CHECK(Loom::Input::GetKey(Loom::Key::W));
		CHECK_FALSE(Loom::Input::GetKey(Loom::Key::None));

		Loom::Input::OnKey(w_key, false);
		Loom::Input::Tick();

		CHECK(Loom::Input::GetKeyUp(Loom::Key::W));

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

	TEST_CASE("the mouse is measured in the view the game is drawn in")
	{
		constexpr int window_width = 1280;
		constexpr int window_height = 720;
		constexpr int view_x = 100;
		constexpr int view_y = 50;
		constexpr int view_width = 640;
		constexpr int view_height = 360;
		constexpr int cursor_x = 300;
		constexpr int cursor_y = 200;

		Loom::Input::OnMouseMove(cursor_x, cursor_y, window_width, window_height);
		Loom::Input::SetView(view_x, view_y, view_width, view_height);

		CHECK(Loom::Input::mouse_x == cursor_x - view_x);
		CHECK(Loom::Input::mouse_y == cursor_y - view_y);
		CHECK(Loom::Input::screen_width == view_width);
		CHECK(Loom::Input::screen_height == view_height);

		// A move keeps to the view rather than the whole window.
		Loom::Input::OnMouseMove(view_x, view_y, window_width, window_height);

		CHECK(Loom::Input::mouse_x == 0);
		CHECK(Loom::Input::mouse_y == 0);
		CHECK(Loom::Input::screen_width == view_width);

		Loom::Input::ClearView();

		CHECK(Loom::Input::mouse_x == view_x);
		CHECK(Loom::Input::screen_width == window_width);
		CHECK(Loom::Input::screen_height == window_height);
	};

	TEST_CASE("a view of no size has the cursor outside it")
	{
		constexpr int window_width = 1280;
		constexpr int window_height = 720;
		constexpr int cursor_x = 300;
		constexpr int cursor_y = 200;

		Loom::Input::OnMouseMove(cursor_x, cursor_y, window_width, window_height);
		Loom::Input::SetView(0, 0, 0, 0);

		CHECK(Loom::Input::mouse_x >= Loom::Input::screen_width);
		CHECK(Loom::Input::mouse_y >= Loom::Input::screen_height);

		Loom::Input::ClearView();
	};

	TEST_CASE("a resize sizes the whole window and leaves the cursor where it was")
	{
		constexpr int window_width = 1280;
		constexpr int window_height = 720;
		constexpr int resized_width = 2560;
		constexpr int resized_height = 1440;
		constexpr int cursor_x = 300;
		constexpr int cursor_y = 200;

		Loom::Input::ClearView();
		Loom::Input::OnMouseMove(cursor_x, cursor_y, window_width, window_height);
		Loom::Input::OnResize(resized_width, resized_height);

		CHECK(Loom::Input::screen_width == resized_width);
		CHECK(Loom::Input::screen_height == resized_height);
		CHECK(Loom::Input::mouse_x == cursor_x);
		CHECK(Loom::Input::mouse_y == cursor_y);
	};
};

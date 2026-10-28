// Cases that need a real page: a canvas, the DOM's input events, and a
// cross-origin isolated context for threads. run-browser-tests.ps1 builds them
// with the rest of the web suite and runs it in headless Chrome, where
// run.mjs sends real input through Chrome's own pipeline when a case asks.

#include "doctest.h"

#include "Engine.h"
#include "Input.h"
#include "OpenGL.h"
#include "Utilities/DialogueInput.h"

#include <emscripten.h>

#include <functional>
#include <string>


namespace
{
	constexpr int frame_ms = 16;
	constexpr int timeout_ms = 5000;

	// Where the mouse is sent, in CSS pixels over the full-page canvas.
	constexpr int pointer_x = 200;
	constexpr int pointer_y = 150;

	// GLFW's numbering, which Input keeps.
	constexpr int left_button = 0;
	constexpr int right_button = 1;
	constexpr int middle_button = 2;
	constexpr int w_key = 'W';

	// Every case shares the page's one canvas, so the engine is made once.
	void OpenPage()
	{
		static Loom::Engine engine;
	};

	// Asks run.mjs for one piece of input. It polls for this, sends it, and
	// clears it.
	void Request(const std::string& action)
	{
		emscripten_run_script(("Module.loomRequest = '" + action + "';").c_str());
	};

	// Runs the engine's own frames until the condition holds or time runs out,
	// so the input is read exactly the way a game reads it.
	bool Until(const std::function<bool()>& condition)
	{
		for (int waited = 0; waited < timeout_ms; waited += frame_ms)
		{
			emscripten_sleep(frame_ms);

			Loom::Engine::renderFrame();

			if (condition())
				return true;
		};

		return false;
	};

	bool KeyIs(int key, int state)
	{
		return glfwGetKey(Loom::Engine::window, key) == state;
	};

	// Every keydown the page has had, repeats among them.
	int Keydowns()
	{
		return EM_ASM_INT({ return Module.loomKeydowns; });
	};

	// The wheel's notches over the frames until it turns, or none.
	float Turned()
	{
		float turned = 0.0f;

		Until(
			[&]()
			{
				turned += Loom::Input::GetMouseWheel();
				return turned != 0.0f;
			});

		return turned;
	};
};

TEST_SUITE("Browser")
{
	TEST_CASE("the page is cross-origin isolated, which threads need")
	{
		CHECK(EM_ASM_INT({ return crossOriginIsolated ? 1 : 0; }) == 1);
	};

	TEST_CASE("a mouse move over the canvas reaches Input")
	{
		OpenPage();

		Request("move:" + std::to_string(pointer_x) + "," + std::to_string(pointer_y));

		CHECK(Until([]() { return Loom::Input::mouse_x == pointer_x && Loom::Input::mouse_y == pointer_y; }));
	};

	TEST_CASE("a left click is down, held, then up")
	{
		OpenPage();

		Request("press:left");

		REQUIRE(Until([]() { return Loom::Input::GetMouseButtonDown(left_button); }));
		CHECK(Loom::Input::GetMouseButton(left_button));

		Request("release:left");

		CHECK(Until([]() { return Loom::Input::GetMouseButtonUp(left_button); }));
		CHECK_FALSE(Loom::Input::GetMouseButton(left_button));
	};

	TEST_CASE("the right and middle buttons are told apart from the left")
	{
		OpenPage();

		Request("press:right");

		CHECK(Until([]() { return Loom::Input::GetMouseButtonDown(right_button); }));
		CHECK_FALSE(Loom::Input::GetMouseButton(left_button));

		Request("release:right");
		CHECK(Until([]() { return !Loom::Input::GetMouseButton(right_button); }));

		Request("press:middle");

		CHECK(Until([]() { return Loom::Input::GetMouseButtonDown(middle_button); }));

		Request("release:middle");
		CHECK(Until([]() { return !Loom::Input::GetMouseButton(middle_button); }));
	};

	TEST_CASE("the spacebar reaches GLFW and the dialogue input")
	{
		OpenPage();

		Request("keydown:Space");

		CHECK(Until([]() { return KeyIs(GLFW_KEY_SPACE, GLFW_PRESS) && Loom::DialogueInput::ContinuedOnKeyboard(); }));

		Request("keyup:Space");

		CHECK(Until([]() { return KeyIs(GLFW_KEY_SPACE, GLFW_RELEASE); }));
	};

	TEST_CASE("a left click reaches the dialogue input")
	{
		OpenPage();

		Request("press:left");

		CHECK(Until([]() { return Loom::DialogueInput::Clicked(); }));

		Request("release:left");
		CHECK(Until([]() { return !Loom::Input::GetMouseButton(left_button); }));
	};

	TEST_CASE("a key is down, held through its repeats, then up")
	{
		OpenPage();

		Request("keydown:KeyW");

		REQUIRE(Until([]() { return Loom::Input::GetKeyDown(w_key); }));
		CHECK(Loom::Input::GetKey(w_key));

		const int keydowns = Keydowns();
		bool pressed = false;

		Request("repeat:KeyW");

		CHECK(Until(
			[&]()
			{
				pressed = pressed || Loom::Input::GetKeyDown(w_key);
				return Keydowns() > keydowns;
			}));

		CHECK_FALSE(pressed);
		CHECK(Loom::Input::GetKey(w_key));

		Request("keyup:KeyW");

		CHECK(Until([]() { return Loom::Input::GetKeyUp(w_key); }));
		CHECK_FALSE(Loom::Input::GetKey(w_key));
	};

	TEST_CASE("the wheel turns in notches of a hundred pixels, small turns and all")
	{
		constexpr float one_notch = 1.0f;
		constexpr float tenth_of_a_notch = 0.1f;

		OpenPage();

		Request("move:" + std::to_string(pointer_x) + "," + std::to_string(pointer_y));
		REQUIRE(Until([]() { return Loom::Input::mouse_x == pointer_x && Loom::Input::mouse_y == pointer_y; }));

		// Up the page is away from the person.
		Request("wheel:-100");
		CHECK(Turned() == doctest::Approx(one_notch));

		Request("wheel:10");
		CHECK(Turned() == doctest::Approx(-tenth_of_a_notch));
	};

	TEST_CASE("the window takes the canvas's device pixels, as a zoomed page backs it")
	{
		constexpr int zoom = 2;

		OpenPage();

		EM_ASM(
			{
				const canvas = Module.canvas;
				canvas.width = canvas.clientWidth * $0;
				canvas.height = canvas.clientHeight * $0;
				window.dispatchEvent(new Event("resize"));
			},
			zoom);

		const int width = EM_ASM_INT({ return Module.canvas.clientWidth; }) * zoom;
		const int height = EM_ASM_INT({ return Module.canvas.clientHeight; }) * zoom;

		CHECK(Until(
			[&]()
			{
				int framebufferWidth, framebufferHeight;
				glfwGetFramebufferSize(Loom::Engine::window, &framebufferWidth, &framebufferHeight);
				return framebufferWidth == width && framebufferHeight == height;
			}));

		CHECK(EM_ASM_INT({ return Module.canvas.width; }) == width);
		CHECK(EM_ASM_INT({ return Module.canvas.height; }) == height);

		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		CHECK(viewport[2] == width);
		CHECK(viewport[3] == height);
	};
};

#include "Input.h"

#include "Engine.h"

#include "OpenGL.h"

#include <array>
#include <iostream>
#include <optional>
#include <utility>


namespace Loom
{
	namespace
	{
		static_assert(Input::mouse_buttons == GLFW_MOUSE_BUTTON_LAST + 1);
		static_assert(Input::keys == GLFW_KEY_LAST + 1);

		// Key follows GLFW's numbering through each run of it.
		static_assert((int)Key::None == GLFW_KEY_UNKNOWN);
		static_assert((int)Key::Space == GLFW_KEY_SPACE);
		static_assert((int)Key::Digit9 == GLFW_KEY_9);
		static_assert((int)Key::Semicolon == GLFW_KEY_SEMICOLON);
		static_assert((int)Key::Equal == GLFW_KEY_EQUAL);
		static_assert((int)Key::Z == GLFW_KEY_Z);
		static_assert((int)Key::RightBracket == GLFW_KEY_RIGHT_BRACKET);
		static_assert((int)Key::GraveAccent == GLFW_KEY_GRAVE_ACCENT);
		static_assert((int)Key::End == GLFW_KEY_END);
		static_assert((int)Key::Pause == GLFW_KEY_PAUSE);
		static_assert((int)Key::F25 == GLFW_KEY_F25);
		static_assert((int)Key::KeypadEqual == GLFW_KEY_KP_EQUAL);
		static_assert((int)Key::Menu == GLFW_KEY_MENU);

		bool has_focus = true;

		// The mouse's buttons or the keyboard's keys, numbered from 0: what
		// the callbacks said of each, and what this frame answers for it.
		template <int count>
		struct Switches
		{
			using Flags = std::array<bool, count>;

			static bool Exists(int which)
			{
				return which >= 0 && which < count;
			};

			bool Down(int which) const
			{
				return has_focus && Exists(which) && down_this_frame[which];
			};

			bool Held(int which) const
			{
				return has_focus && Exists(which) && (held[which] || down_this_frame[which]);
			};

			bool Up(int which) const
			{
				return Exists(which) && (has_focus ? up_this_frame[which] : lost_this_frame[which]);
			};

			void Heard(int which, bool pressed)
			{
				if (!Exists(which))
					return;

				held[which] = pressed;

				if (pressed)
					went_down[which] = true;
				else
					went_up[which] = true;
			};

			void HeardThisFrame(int which, bool pressed)
			{
				if (!Exists(which))
					return;

				held[which] = pressed;

				if (pressed)
					down_this_frame[which] = true;
				else
					up_this_frame[which] = true;
			};

			// While focus is still had, as it goes.
			void LoseFocus()
			{
				for (int which = 0; which < count; which++)
					lost_this_frame[which] = Held(which) && !down_this_frame[which];
			};

			void Tick()
			{
				down_this_frame = went_down;
				up_this_frame = went_up;

				went_down.fill(false);
				went_up.fill(false);
				lost_this_frame.fill(false);
			};

			// Held, as the callbacks last said.
			Flags held{ };

			// Edges heard since the last Tick, and the ones this frame answers with.
			Flags went_down{ };
			Flags went_up{ };
			Flags down_this_frame{ };
			Flags up_this_frame{ };

			// Held when focus went, which the frame it went in hears come up. A
			// press in that frame took focus away, so the game never had it.
			Flags lost_this_frame{ };
		};

		Switches<Input::mouse_buttons> buttons;
		Switches<Input::keys> keyboard;

		// Notches heard since the last Tick, and the ones this frame answers with.
		float wheel_turned = 0.0f;
		float wheel_this_frame = 0.0f;

		struct Rect
		{
			int x = 0;
			int y = 0;
			int width = 0;
			int height = 0;
		};

		// In the window's pixels, as the callbacks last said.
		int cursor_x = 0;
		int cursor_y = 0;
		Rect whole_window;

		std::optional<Rect> view;

		void Place()
		{
			const Rect& seen = view ? *view : whole_window;

			Input::mouse_x = cursor_x - seen.x;
			Input::mouse_y = cursor_y - seen.y;
			Input::screen_width = seen.width;
			Input::screen_height = seen.height;
		};

		bool IsCursorInView()
		{
			return Input::mouse_x >= 0 && Input::mouse_y >= 0 && Input::mouse_x < Input::screen_width && Input::mouse_y < Input::screen_height;
		};

		void mouse_button_callback(GLFWwindow*, int button, int action, int)
		{
			Input::OnMouseButton(button, action == GLFW_PRESS);
		};

		void key_callback(GLFWwindow*, int key, int, int action, int)
		{
			// A key held long enough repeats, which is no new press.
			if (action != GLFW_REPEAT)
				Input::OnKey(key, action == GLFW_PRESS);
		};
	};

	bool Input::GetMouseButtonDown(int button)
	{
		return buttons.Down(button);
	};

	bool Input::GetMouseButton(int button)
	{
		return buttons.Held(button);
	};

	bool Input::GetMouseButtonUp(int button)
	{
		return buttons.Up(button);
	};

	void Input::OnMouseButton(int button, bool pressed)
	{
		buttons.Heard(button, pressed);
	};

	void Input::OnMouseButtonThisFrame(int button, bool pressed)
	{
		buttons.HeardThisFrame(button, pressed);
	};

	bool Input::GetKeyDown(int key)
	{
		return keyboard.Down(key);
	};

	bool Input::GetKey(int key)
	{
		return keyboard.Held(key);
	};

	bool Input::GetKeyUp(int key)
	{
		return keyboard.Up(key);
	};

	void Input::OnKey(int key, bool pressed)
	{
		keyboard.Heard(key, pressed);
	};

	void Input::OnKeyThisFrame(int key, bool pressed)
	{
		keyboard.HeardThisFrame(key, pressed);
	};

	float Input::GetMouseWheel()
	{
		return has_focus && IsCursorInView() ? wheel_this_frame : 0.0f;
	};

	void Input::OnMouseWheel(float notches)
	{
		wheel_turned += notches;
	};

	void Input::OnMouseWheelThisFrame(float notches)
	{
		wheel_this_frame += notches;
	};

	void Input::OnMouseMove(int x, int y, int window_width, int window_height)
	{
		cursor_x = x;
		cursor_y = y;
		OnResize(window_width, window_height);
	};

	void Input::OnResize(int window_width, int window_height)
	{
		whole_window = { 0, 0, window_width, window_height };
		Place();
	};

	void Input::SetView(int x, int y, int width, int height)
	{
		view = { x, y, width, height };
		Place();
	};

	void Input::ClearView()
	{
		view.reset();
		Place();
	};

	void Input::SetFocused(bool focused)
	{
		if (has_focus && !focused)
		{
			buttons.LoseFocus();
			keyboard.LoseFocus();
		};

		has_focus = focused;
	};

	bool Input::IsFocused()
	{
		return has_focus;
	};

	void Input::Tick()
	{
		buttons.Tick();
		keyboard.Tick();

		wheel_this_frame = std::exchange(wheel_turned, 0.0f);
	};

#if __EMSCRIPTEN__
	EM_BOOL mouse_move_callback(int eventType, const EmscriptenMouseEvent* e, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE)
		{
			// In the canvas's own pixels, as the renderer and a resize measure
			// it, which on a dense screen are more than the page's.
			int width = 0;
			int height = 0;
			double cssWidth = 0.0;
			double cssHeight = 0.0;

			if (emscripten_get_canvas_element_size("#canvas", &width, &height) != EMSCRIPTEN_RESULT_SUCCESS ||
				emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS ||
				cssWidth <= 0.0 || cssHeight <= 0.0)
			{
				std::cerr << "Failed to get the canvas's size!" << std::endl;
				return EM_FALSE;
			};

			Input::OnMouseMove((int)(e->clientX * width / cssWidth), (int)(e->clientY * height / cssHeight), width, height);
		};

		return EM_TRUE;
	};

	// The DOM's own wheel event: GLFW's port rounds every one up to a whole
	// notch, which turns a touchpad's run of small ones into a spin.
	EM_BOOL wheel_callback(int, const EmscriptenWheelEvent* e, void*)
	{
		// How far a notch goes in each of the DOM's units, as SDL and ImGui's
		// GLFW backend count it.
		constexpr double pixels_per_notch = 100.0;
		constexpr double lines_per_notch = 3.0;
		constexpr double notches_per_page = 80.0;

		const double notches =
			e->deltaMode == DOM_DELTA_PIXEL ? e->deltaY / pixels_per_notch :
			e->deltaMode == DOM_DELTA_LINE ? e->deltaY / lines_per_notch :
			e->deltaY * notches_per_page;

		// The DOM's is positive towards the person.
		Input::OnMouseWheel((float)-notches);

		return EM_TRUE;
	};
#else
	void mouse_move_callback(
		GLFWwindow* window,
		double xpos,
		double ypos)
	{
		int width, height;
		glfwGetWindowSize(window, &width, &height);

		Input::OnMouseMove((int)xpos, (int)ypos, width, height);
	};

	void scroll_callback(GLFWwindow*, double, double notches)
	{
		Input::OnMouseWheel((float)notches);
	};
#endif

	void Input::Init()
	{
#if __EMSCRIPTEN__
		printf("Registering Emscripten callbacks globally...\n");

		// Register mouse move callback for global window events
		emscripten_set_mousemove_callback(
			EMSCRIPTEN_EVENT_TARGET_WINDOW, // Attach to the global window
			nullptr,
			true,
			mouse_move_callback);

		emscripten_set_wheel_callback(
			"#canvas",
			nullptr,
			false,
			wheel_callback);
#else
		glfwSetCursorPosCallback(
			Engine::window,
			mouse_move_callback);

		glfwSetScrollCallback(
			Engine::window,
			scroll_callback);
#endif

		// The engine's own GLFW port on the web: ImGui is told to leave GLFW's
		// callbacks alone there, and on the desktop it chains to these.
		glfwSetMouseButtonCallback(
			Engine::window,
			mouse_button_callback);

		glfwSetKeyCallback(
			Engine::window,
			key_callback);
	};
};

#include "Input.h"

#include "Engine.h"

#include "OpenGL.h"

#include "Globals.h"

#include <array>
#include <iostream>


namespace Loom
{
	namespace
	{
		static_assert(Input::mouse_buttons == GLFW_MOUSE_BUTTON_LAST + 1);

		using Buttons = std::array<bool, Input::mouse_buttons>;

		// Held, as the callbacks last said.
		Buttons held{ };

		// Edges heard since the last Tick, and the ones this frame answers with.
		Buttons went_down{ };
		Buttons went_up{ };
		Buttons down_this_frame{ };
		Buttons up_this_frame{ };

		bool IsButton(int button)
		{
			return button >= 0 && button < Input::mouse_buttons;
		};

		void mouse_button_callback(GLFWwindow*, int button, int action, int)
		{
			Input::OnMouseButton(button, action == GLFW_PRESS);
		};
	};

	bool Input::GetMouseButtonDown(int button)
	{
		return IsButton(button) && down_this_frame[button];
	};

	bool Input::GetMouseButton(int button)
	{
		return IsButton(button) && (held[button] || down_this_frame[button]);
	};

	bool Input::GetMouseButtonUp(int button)
	{
		return IsButton(button) && up_this_frame[button];
	};

	void Input::OnMouseButton(int button, bool pressed)
	{
		if (!IsButton(button))
			return;

		held[button] = pressed;

		if (pressed)
			went_down[button] = true;
		else
			went_up[button] = true;
	};

	void Input::Tick()
	{
		down_this_frame = went_down;
		up_this_frame = went_up;

		went_down.fill(false);
		went_up.fill(false);
	};

#if __EMSCRIPTEN__
	EM_BOOL mouse_move_callback(int eventType, const EmscriptenMouseEvent* e, void* userData)
	{
		if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE)
		{
			double windowWidth, windowHeight;

			// Get the current window size
			EMSCRIPTEN_RESULT res = emscripten_get_element_css_size(
				"body",
				&windowWidth,
				&windowHeight);

			if (res != EMSCRIPTEN_RESULT_SUCCESS)
			{
				std::cerr << "Failed to get window size!" << std::endl;
				return EM_FALSE;
			};

			Input::screen_width = windowWidth;
			Input::screen_height = windowHeight;

			Input::mouse_x = e->clientX;
			Input::mouse_y = e->clientY;
		};

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

		Input::screen_width = width;
		Input::screen_height = height;

		Input::mouse_x = (int)xpos;
		Input::mouse_y = (int)ypos;
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
#else
		glfwSetCursorPosCallback(
			Engine::window,
			mouse_move_callback);
#endif

		// The engine's own GLFW port on the web: ImGui is told to leave GLFW's
		// callbacks alone there, and on the desktop it chains to this one.
		glfwSetMouseButtonCallback(
			Engine::window,
			mouse_button_callback);
	};
};

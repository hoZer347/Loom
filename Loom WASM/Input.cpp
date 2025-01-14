#include "Input.h"

#include "Engine.h"

#if __EMSCRIPTEN__
#include <emscripten/html5.h>
#else
#include "OpenGL.h"
#endif

#include "Globals.h"

#include <iostream>


namespace Loom
{
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
	};
};

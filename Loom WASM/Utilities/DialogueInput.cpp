#include "DialogueInput.h"

#include "Engine.h"

#include "OpenGL.h"


namespace Loom
{
	// The keys "carry on" is spelled with. One list, so the stream and the wait cannot
	// disagree about what advances a line.
	static const int s_keys[] =
	{
		GLFW_KEY_SPACE,
		GLFW_KEY_1,
		GLFW_KEY_2,
		GLFW_KEY_3,
		GLFW_KEY_KP_1,
		GLFW_KEY_KP_2,
		GLFW_KEY_KP_3,
	};

	static bool s_keyboard = false;
	static bool s_keyboard_last = false;

	static bool s_pad = false;
	static bool s_pad_last = false;

	static bool s_mouse = false;
	static bool s_mouse_last = false;

	static bool s_keyboard_pressed = false;
	static bool s_pad_pressed = false;
	static bool s_mouse_pressed = false;

	bool DialogueInput::Continued() { return s_pad_pressed; };

	bool DialogueInput::ContinuedOnKeyboard() { return s_keyboard_pressed; };

	bool DialogueInput::ContinuedAnyhow() { return ContinuedOnKeyboard() || Continued(); };

	bool DialogueInput::Clicked() { return s_mouse_pressed; };

	void DialogueInput::Tick()
	{
		GLFWwindow* window = Engine::window;

		s_keyboard_last = s_keyboard;
		s_pad_last = s_pad;
		s_mouse_last = s_mouse;

		s_keyboard = false;
		s_pad = false;
		s_mouse = false;

		if (window != nullptr)
		{
			for (const int key : s_keys)
				if (glfwGetKey(window, key) == GLFW_PRESS)
				{
					s_keyboard = true;

					break;
				};

			s_mouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		};

#ifndef __EMSCRIPTEN__
		// The four lettered face buttons, by position, on the first pad that is one.
		// Guarded off under Emscripten, whose GLFW port has no gamepad mapping at all --
		// the browser reads pads through its own API, which is a separate job.
		for (int pad = GLFW_JOYSTICK_1; pad <= GLFW_JOYSTICK_LAST && !s_pad; pad++)
		{
			if (!glfwJoystickPresent(pad) || !glfwJoystickIsGamepad(pad))
				continue;

			GLFWgamepadstate state{ };

			if (!glfwGetGamepadState(pad, &state))
				continue;

			s_pad =
				state.buttons[GLFW_GAMEPAD_BUTTON_A]
				|| state.buttons[GLFW_GAMEPAD_BUTTON_B]
				|| state.buttons[GLFW_GAMEPAD_BUTTON_X]
				|| state.buttons[GLFW_GAMEPAD_BUTTON_Y];
		};
#endif

		s_keyboard_pressed = s_keyboard && !s_keyboard_last;
		s_pad_pressed = s_pad && !s_pad_last;
		s_mouse_pressed = s_mouse && !s_mouse_last;
	};
};

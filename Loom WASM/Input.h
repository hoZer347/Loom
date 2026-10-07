#pragma once

#include "Loom API.h"


namespace Loom
{
	struct LOOM_API Input
	{
		// Buttons are numbered as GLFW numbers them: 0 left, 1 right, 2 middle,
		// up to GLFW_MOUSE_BUTTON_LAST. Anything outside that is never pressed.
		static constexpr int mouse_buttons = 8;

		// Went down since the frame before.
		static bool GetMouseButtonDown(int button);

		// Held this frame, which includes a click that went down and came back
		// up between two frames.
		static bool GetMouseButton(int button);

		// Came up since the frame before.
		static bool GetMouseButtonUp(int button);

		// What the platform's button callback reports, as it happens.
		static void OnMouseButton(int button, bool pressed);

		// Hands what OnMouseButton heard since the last call to this frame's
		// questions. Engine calls it once a frame, after polling for events.
		static void Tick();

		static inline int screen_width = 0;
		static inline int screen_height = 0;

		static inline int mouse_x = 0;
		static inline int mouse_y = 0;

	protected:
		friend struct Engine;

		Input() = delete;
		~Input() = delete;

		static void Init();
	};
};

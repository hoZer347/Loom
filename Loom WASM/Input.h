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

		// What the platform's cursor callback reports: the cursor in pixels from
		// the window's top left, and the window's size.
		static void OnMouseMove(int x, int y, int window_width, int window_height);

		// Where the game is drawn in the window, in the window's pixels, which
		// mouse_x, mouse_y, screen_width and screen_height then measure: an
		// editor's Game view, a part of the editor's window, or no size at all
		// while it is hidden. Until it is set, or once it is cleared, they
		// measure the whole window.
		static void SetView(int x, int y, int width, int height);
		static void ClearView();

		// For a press made up after this frame's Tick, such as a UI script's,
		// which this frame hears as though it had been polled with the rest.
		static void OnMouseButtonThisFrame(int button, bool pressed);

		// Whether the game hears the mouse buttons, which DialogueInput also
		// asks before it reports keys and pads: always on its own, and in an
		// editor only while its Game view has focus. Without it nothing is
		// down or held, and what was held as it went comes up.
		static void SetFocused(bool focused);
		static bool IsFocused();

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

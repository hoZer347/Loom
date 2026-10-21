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

		// What the platform's resize callback reports: the window's size.
		static void OnResize(int window_width, int window_height);

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

		// Keys are numbered as GLFW numbers them: a letter or a digit by its
		// character, 'W' or '1', and the rest as GLFW_KEY_ names them, up to
		// GLFW_KEY_LAST. Anything outside that is never pressed.
		static constexpr int keys = 349;

		// The same three questions of a key as of a mouse button.
		static bool GetKeyDown(int key);
		static bool GetKey(int key);
		static bool GetKeyUp(int key);

		// What the platform's key callback reports, as it happens, and a press
		// made up after Tick, as for a mouse button.
		static void OnKey(int key, bool pressed);
		static void OnKeyThisFrame(int key, bool pressed);

		// How far the mouse wheel turned since the frame before, in notches,
		// away from the person positive. Heard only while the cursor is on
		// the view, as the wheel turns whatever is under it.
		static float GetMouseWheel();

		// What the platform's scroll callback reports, as it happens, and a
		// turn made up after Tick, as for a mouse button.
		static void OnMouseWheel(float notches);
		static void OnMouseWheelThisFrame(float notches);

		// Whether the game hears the mouse and the keyboard, which
		// DialogueInput also asks before it reports keys and pads: always on
		// its own, and in an editor only while its Game view has focus.
		// Without it nothing is down or held, what was held as it went comes
		// up, and the wheel turns nothing.
		static void SetFocused(bool focused);
		static bool IsFocused();

		// Hands what the callbacks heard since the last call to this frame's
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

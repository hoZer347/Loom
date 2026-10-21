#pragma once

#include "Loom API.h"

#include <span>
#include <string_view>


namespace Loom
{
	// The keys by name, numbered as GLFW numbers them, so either can be asked
	// of Input. A Serial<Key> is offered in the inspector as a list of them,
	// None for a key that is not set.
	enum struct Key : int
	{
		None = -1,

		Space = 32,
		Apostrophe = 39,
		Comma = 44, Minus, Period, Slash,
		Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
		Semicolon = 59,
		Equal = 61,
		A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		LeftBracket, Backslash, RightBracket,
		GraveAccent = 96,
		World1 = 161, World2,

		Escape = 256, Enter, Tab, Backspace, Insert, Delete,
		Right, Left, Down, Up,
		PageUp, PageDown, Home, End,
		CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,
		F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13,
		F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,

		Keypad0 = 320, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
		KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract, KeypadAdd, KeypadEnter, KeypadEqual,

		LeftShift = 340, LeftControl, LeftAlt, LeftSuper,
		RightShift, RightControl, RightAlt, RightSuper,
		Menu,
	};

	// Every Key, None first and the rest in number order.
	LOOM_API std::span<const Key> AllKeys();

	// A Key by its enumerator's name, "Space" or "LeftControl", and back, as a
	// file of bindings writes it. A name that is no Key is None.
	LOOM_API std::string_view NameOf(Key key);
	LOOM_API Key KeyNamed(std::string_view name);

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

		// Keys are numbered as GLFW numbers them, and as Key names them: a
		// letter or a digit by its character, 'W' or '1', and the rest as
		// GLFW_KEY_ names them, up to GLFW_KEY_LAST. Anything outside that is
		// never pressed.
		static constexpr int keys = (int)Key::Menu + 1;

		// The same three questions of a key as of a mouse button.
		static bool GetKeyDown(int key);
		static bool GetKey(int key);
		static bool GetKeyUp(int key);

		static bool GetKeyDown(Key key) { return GetKeyDown((int)key); };
		static bool GetKey(Key key) { return GetKey((int)key); };
		static bool GetKeyUp(Key key) { return GetKeyUp((int)key); };

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

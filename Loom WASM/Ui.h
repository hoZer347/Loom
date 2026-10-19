#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Serial.h"

#include "glm/glm.hpp"

#include <functional>
#include <string>


namespace Loom
{
	struct UiRect;

	/**
	* Loom::Ui
	* - What the UI components share: the size of the view the game is drawn
	*   in, which rect the cursor is on, and the overlay they are drawn in
	* - Screen space is in pixels from the view's top left, y down
	* - The overlay is drawn over a whole view once every scene in it has
	*   been: the window, and the editor's Game view, but not its Scene,
	*   Prefab or Debug views
	* - The cursor is tested against what the last overlay drew, so the rect
	*   drawn last under it is the one it is on, whatever is beneath
	*/
	struct LOOM_API Ui final
	{
		struct Rect
		{
			glm::vec2 min{ 0.0f };
			glm::vec2 size{ 0.0f };

			bool Contains(const glm::vec2& point) const
			{
				return point.x >= min.x && point.y >= min.y && point.x < min.x + size.x && point.y < min.y + size.y;
			};
		};

		// The view the game is drawn in, as Input measures the mouse in it.
		static Rect Screen();

		// The cursor in screen space.
		static glm::vec2 Pointer();

		// Whether the cursor is on a rect that blocks it, so a game can leave
		// alone a click a menu has taken.
		static bool IsPointerOver();

		// Whether rect is the one the cursor is on: the rect that blocks it
		// drawn last under it, by the overlay of this frame or the one before.
		static bool IsPointedAt(const UiRect& rect);

		// Between these, every UiRect and UiText rendered is queued, then the
		// queue is drawn over everything already drawn, in the order queued.
		static void BeginOverlay();
		static void DrawOverlay();

		Ui() = delete;

	private:
		friend struct UiRect;
		friend struct UiText;

		static void Queue(const ComponentBase* component);
	};

	/**
	* Loom::UiRect
	* - A rectangle on the screen, laid out in its parent GameObject's UiRect,
	*   or in the whole screen when there is none
	* - anchor is the point in the parent (0 to 1 across, from the top left)
	*   that offset is measured from, and pivot the point in the rectangle
	*   (0 to 1 across) that lands there
	* - Filled with color, or a UiButton's tint when there is one beside it;
	*   one with no alpha is not drawn, and only lays out its children
	* - The UiText on its GameObject is drawn over it, and its children over
	*   both
	* - With blocksPointer, the cursor on it is on the UI rather than the
	*   game; without, the cursor passes through it, and so does a click on
	*   a button it has
	*/
	struct LOOM_API UiRect : Component<UiRect, "UI">
	{
		void OnRender() override;

		// Where it lies on the screen now.
		Ui::Rect ScreenRect() const;

		// The rectangle laid out in parent, which is in screen space.
		Ui::Rect LayOut(const Ui::Rect& parent) const;

		// What it is filled with: color, or the tint of the UiButton beside it.
		glm::vec4 Fill() const;

		Serial<glm::vec2> anchor = glm::vec2(0.5f, 0.5f);
		Serial<glm::vec2> pivot = glm::vec2(0.5f, 0.5f);
		Serial<glm::vec2> offset = glm::vec2(0.0f, 0.0f);
		Serial<glm::vec2> size = glm::vec2(160.0f, 40.0f);
		Serial<glm::vec4> color = glm::vec4(0.0f, 0.0f, 0.0f, 0.6f);
		Serial<bool> blocksPointer = true;
	};

	/**
	* Loom::UiText
	* - One line of text in the UiRect on its GameObject, or the whole screen
	*   without one, centred on it upright and aligned across it
	* - fontPath names a TrueType file, relative to the project folder; the
	*   printable ASCII characters are baked at fontSize pixels the first time
	*   a size is asked for, and the rest of the text is left out
	* - A baked size is shared by every UiText that uses it, and let go once
	*   none has for a while
	*/
	struct LOOM_API UiText : Component<UiText, "UI">
	{
		enum Alignment : int { Left, Centre, Right };

		// A baked size neither drawn nor measured for this many frames is let go.
		static constexpr unsigned long long unusedFrames = 600;

		void OnRender() override;

		// The text's width and line height in pixels, or zero when the font
		// will not load.
		glm::vec2 Measure() const;

		// Where the text is laid out: its UiRect, or the whole screen.
		Ui::Rect Area() const;

		Serial<std::string> text = std::string("Text");
		Serial<std::string> fontPath;
		Serial<float> fontSize = 24.0f;
		Serial<glm::vec4> color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		Serial<Alignment> alignment = Centre;
	};

	/**
	* Loom::UiButton
	* - Makes the UiRect on its GameObject a button: hovered while the cursor
	*   is on that rect, tinted for it, and clicked by a left click there
	* - onClick runs on the frame it is clicked, and WasClicked says so all
	*   that frame to anything that would rather ask
	*/
	struct LOOM_API UiButton : Component<UiButton, "UI">
	{
		void OnUpdate() override;

		bool IsHovered() const;
		bool WasClicked() const;

		// normalColor, hoverColor or pressedColor, as the cursor is now.
		glm::vec4 Tint() const;

		std::function<void()> onClick;

		Serial<glm::vec4> normalColor = glm::vec4(0.1f, 0.1f, 0.15f, 0.8f);
		Serial<glm::vec4> hoverColor = glm::vec4(0.25f, 0.3f, 0.45f, 0.9f);
		Serial<glm::vec4> pressedColor = glm::vec4(0.4f, 0.45f, 0.65f, 1.0f);
	};
};

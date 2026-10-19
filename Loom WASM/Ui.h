#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Serial.h"

#include "glm/glm.hpp"

#include <functional>
#include <string>


namespace Loom
{
	/**
	* Loom::Ui
	* - What the UI components share: the size of the view the game is drawn
	*   in, and the pass that draws them over it
	* - Screen space is in pixels from the view's top left, y down
	* - Drawn only when a scene is seen through its own camera, so the game
	*   view shows the UI and the editor's Scene view does not
	*/
	struct LOOM_API Ui final
	{
		struct Rect
		{
			glm::vec2 min{ 0.0f };
			glm::vec2 size{ 0.0f };

			bool Contains(const glm::vec2& point) const;
		};

		// The view the game is drawn in, as Input measures the mouse in it.
		static Rect Screen();

		// The cursor in screen space.
		static glm::vec2 Pointer();

		// Whether the cursor was over a UiRect that blocks it last frame, so a
		// game can leave a click alone that a menu has taken.
		static bool IsPointerOver();

		// Between these, every UiRect and UiText rendered is queued, then the
		// queue is drawn over what the scene drew, in the order it was rendered.
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
	* - Filled with color; a clear one lays out its children without drawing
	* - With blocksPointer, Ui::IsPointerOver is true while the cursor is on it
	*/
	struct LOOM_API UiRect : Component<UiRect, "UI">
	{
		void OnUpdate() override;
		void OnRender() override;

		// Where it lies on the screen now.
		Ui::Rect ScreenRect() const;

		// The rectangle laid out in parent, which is in screen space.
		Ui::Rect LayOut(const Ui::Rect& parent) const;

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
	*   a size is asked for, and kept for every UiText that uses it
	*/
	struct LOOM_API UiText : Component<UiText, "UI">
	{
		enum Alignment { Left, Centre, Right };

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
		Serial<int> alignment = Centre;
	};

	/**
	* Loom::UiButton
	* - Makes the UiRect on its GameObject a button: tints it as the cursor is
	*   over it or pressing it, and reports a left click on it
	* - onClick runs on the frame it is clicked, and WasClicked says so to
	*   anything that would rather ask
	*/
	struct LOOM_API UiButton : Component<UiButton, "UI">
	{
		void OnUpdate() override;

		bool IsHovered() const { return m_hovered; };
		bool WasClicked() const { return m_clicked; };

		std::function<void()> onClick;

		Serial<glm::vec4> normalColor = glm::vec4(0.1f, 0.1f, 0.15f, 0.8f);
		Serial<glm::vec4> hoverColor = glm::vec4(0.25f, 0.3f, 0.45f, 0.9f);
		Serial<glm::vec4> pressedColor = glm::vec4(0.4f, 0.45f, 0.65f, 1.0f);

	private:
		bool m_hovered = false;
		bool m_clicked = false;
	};
};

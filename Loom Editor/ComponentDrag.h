#pragma once

#include "Guid.h"

#include "imgui.h"
#include "imgui_internal.h"


namespace Loom
{
	struct ComponentBase;
	struct GameObject;

	/**
	* Loom::ComponentDrag
	* - Dragging a component in the Inspector to reorder a GameObject's
	*   components
	* - While one is dragged every header shows folded, so the whole list is in
	*   sight, and each opens again after as it was
	* - Held near the Inspector's top or bottom, the view scrolls on, by whole
	*   pixels as ImGui does, with what a fast frame falls short of carried
	*   over to the next
	*/
	namespace ComponentDrag
	{
		// Held within this many rows of the Inspector's top or bottom, it
		// scrolls on this many rows a second.
		constexpr float edge_rows = 2.0f;
		constexpr float rows_per_second = 12.0f;

		// Pixels to scroll this frame, and the part of one carried to the next.
		struct Scroll
		{
			float pixels = 0.0f;
			float carried = 0.0f;
		};

		// The component with guid when it is one of gameObject's, so a drag
		// carrying it reorders them. Null for anything else.
		ComponentBase* Dragged(const GameObject& gameObject, const Guid& guid);

		// A component header, folded while folded is set, without forgetting
		// whether it was open. Returns whether it is open.
		bool Header(const char* label, ImGuiTreeNodeFlags flags, bool folded);

		// How far the Inspector, its inner rect inner and its rows row tall,
		// scrolls this frame of dt seconds with the mouse at mouse, carrying
		// what the frames before left over. Nothing outside inner's width or
		// between its edge rows.
		Scroll Step(const ImVec2& mouse, const ImRect& inner, float row, float dt, float carried);
	};
};

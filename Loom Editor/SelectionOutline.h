#pragma once

#include "EditorViewport.h"

#include <cstdint>


namespace Loom
{
	struct Camera;
	struct GameObject;

	/**
	* Loom::SelectionOutline
	* - Rings what is selected in the Hierarchy, children included, in a view
	*   the editor draws the scenes into
	* - Draws the selection's silhouette into a mask of its own, then colours
	*   the pixels just outside it, so the ring shows through whatever stands
	*   in front of the selection
	*/
	struct SelectionOutline final
	{
		SelectionOutline() = default;
		~SelectionOutline();

		SelectionOutline(const SelectionOutline&) = delete;
		SelectionOutline& operator=(const SelectionOutline&) = delete;

		// Draws over the target being drawn into, seen through camera. When the
		// selection has anything to draw, leaves that target's depth cleared.
		void Draw(GameObject& selected, const Camera& camera);

	private:
		// Compiles the programs the first time it is asked. False when they
		// will not compile, which is reported once.
		bool Compile();
		void Destroy();

		// Whether anything was drawn.
		bool DrawSilhouettes(GameObject& gameObject);

		EditorViewport m_mask;

		uint32_t m_meshMask = 0;
		uint32_t m_spriteMask = 0;
		uint32_t m_edge = 0;

		bool m_compileTried = false;
	};
};

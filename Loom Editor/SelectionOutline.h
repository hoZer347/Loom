#pragma once

#include "EditorViewport.h"


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
		// Draws over the target being drawn into, seen through camera. Leaves
		// that target's depth cleared.
		void Draw(GameObject& selected, const Camera& camera);

	private:
		EditorViewport m_mask;
	};
};

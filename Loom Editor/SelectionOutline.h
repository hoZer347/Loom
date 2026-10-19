#pragma once

#include "EditorViewport.h"

#include <cstdint>
#include <unordered_map>


namespace Loom
{
	struct Camera;
	struct GameObject;
	struct Shader;

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
		bool DrawSilhouettes(GameObject& gameObject, const Camera& camera);

		// The mask program for meshes drawn with shader: its own vertex stage,
		// so the silhouette lands where the mesh does, or the plain one when
		// that will not compile.
		uint32_t MeshMask(const Shader& shader);

		// A mask program, and the shader program it was made beside, which a
		// reloaded shader no longer matches.
		struct ShaderMask
		{
			uint32_t shader = 0;
			uint32_t program = 0;
		};

		EditorViewport m_mask;

		std::unordered_map<const Shader*, ShaderMask> m_meshMasks;
		uint32_t m_meshMask = 0;
		uint32_t m_spriteMask = 0;
		uint32_t m_edge = 0;

		bool m_compileTried = false;
	};
};

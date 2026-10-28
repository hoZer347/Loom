#pragma once

#include "EditorViewport.h"

#include <cstdint>
#include <unordered_map>
#include <vector>


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
	* - Where the selection runs off the view, the ring follows the view's
	*   edge inside it, so a part cut off by the edge is ringed too
	*/
	struct SelectionOutline final
	{
		#pragma region Lifetime

		SelectionOutline() = default;
		~SelectionOutline();

		SelectionOutline(const SelectionOutline&) = delete;
		SelectionOutline& operator=(const SelectionOutline&) = delete;

		#pragma endregion

		#pragma region Drawing

		// Draws over the target being drawn into, seen through camera. When the
		// selection has anything to draw, leaves that target's depth cleared.
		void Draw(GameObject& selected, const Camera& camera);

		// One ring around all of them, in one pass.
		void Draw(const std::vector<GameObject*>& selected, const Camera& camera);

		#pragma endregion

	private:
		#pragma region Programs

		// A mask program, its twin for meshes the GPU bends or 0 without one,
		// and the shader program they were made beside, which a reloaded
		// shader no longer matches.
		struct ShaderMask
		{
			uint32_t shader = 0;
			uint32_t program = 0;
			uint32_t skinned = 0;
		};

		// Compiles the programs the first time it is asked. False when they
		// will not compile, which is reported once.
		bool Compile();
		void Destroy();

		// The mask programs for meshes drawn with shader: its own vertex
		// stages, so the silhouette lands where the mesh does, or the plain one
		// when that will not compile.
		const ShaderMask& MeshMask(const Shader& shader);

		#pragma endregion

		#pragma region Drawing

		// Whether anything was drawn.
		bool DrawSilhouettes(GameObject& gameObject, const Camera& camera);

		#pragma endregion

		#pragma region Programs

		std::unordered_map<const Shader*, ShaderMask> m_meshMasks;
		uint32_t m_meshMask = 0;
		uint32_t m_spriteMask = 0;
		uint32_t m_edge = 0;

		bool m_compileTried = false;

		#pragma endregion

		#pragma region Drawing

		EditorViewport m_mask;

		#pragma endregion
	};
};

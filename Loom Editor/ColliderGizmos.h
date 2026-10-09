#pragma once

#include "glm/glm.hpp"

#include <cstdint>
#include <vector>


namespace Loom
{
	struct Camera;
	struct GameObject;

	/**
	* Loom::ColliderGizmos
	* - Outlines every collider on what is selected in the Hierarchy, children
	*   included, in green over a view the editor draws the scenes into, as
	*   Unity does
	* - Drawn over everything, so a collider inside its mesh still shows
	*/
	struct ColliderGizmos final
	{
		ColliderGizmos() = default;
		~ColliderGizmos();

		ColliderGizmos(const ColliderGizmos&) = delete;
		ColliderGizmos& operator=(const ColliderGizmos&) = delete;

		// Into the target being drawn into, seen through camera. When there is
		// anything to draw, leaves that target's depth cleared.
		void Draw(GameObject& selected, const Camera& camera);

	private:
		// Compiles the program the first time it is asked. False when it will
		// not compile, which is reported once.
		bool Compile();

		void Gather(GameObject& gameObject);

		uint32_t m_program = 0;
		bool m_compileTried = false;

		// Every line's two ends, kept between frames to save reallocating.
		std::vector<glm::vec3> m_lines{ };
	};
};

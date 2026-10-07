#pragma once

#include "glm/glm.hpp"

#include <cmath>


namespace Loom
{
	// The order glGetIntegerv(GL_VIEWPORT) fills in.
	enum Viewport { VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, VIEWPORT_SIZE };

	constexpr glm::vec3 X_AXIS(1.0f, 0.0f, 0.0f);
	constexpr glm::vec3 Y_AXIS(0.0f, 1.0f, 0.0f);
	constexpr glm::vec3 Z_AXIS(0.0f, 0.0f, 1.0f);

	// An up for lookAt along a normalised forward. lookAt needs one that is
	// not parallel to the view, which world up is when looking straight down.
	inline glm::vec3 UpFor(const glm::vec3& forward)
	{
		constexpr float VERTICAL = 0.99f;

		return std::abs(forward.y) > VERTICAL
			? glm::vec3(0.0f, 0.0f, -1.0f)
			: glm::vec3(0.0f, 1.0f, 0.0f);
	};
};

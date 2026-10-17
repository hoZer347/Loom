#pragma once

#include "RenderMath.h"
#include "Serial.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"


namespace Loom
{
	/**
	* Loom::Transform
	* - Where a GameObject sits relative to its parent
	* - Rotation is Euler angles in degrees, which is how the inspector edits it,
	*   applied Z, then X, then Y
	* - Its fields register with the GameObject being built around it, so it only
	*   ever lives as a GameObject's member
	*/
	struct Transform final
	{
		// A rotation as the Euler angles rotation holds, in degrees.
		LOOM_API static glm::vec3 EulerDegrees(const glm::quat& rotation);

		Serial<glm::vec3> position;
		Serial<glm::vec3> rotation;
		Serial<glm::vec3> scale = glm::vec3(1.0f);

		// Scale, then rotate, then translate. The rotation is glm's
		// eulerAngleYXZ written out, so no turn about one axis is built and
		// multiplied on its own.
		glm::mat4 Matrix() const
		{
			const glm::vec3 size = *scale;

			// Unturned, as most things that never move are.
			if (*rotation == glm::vec3(0.0f))
				return glm::mat4(
					glm::vec4(size.x, 0.0f, 0.0f, 0.0f),
					glm::vec4(0.0f, size.y, 0.0f, 0.0f),
					glm::vec4(0.0f, 0.0f, size.z, 0.0f),
					glm::vec4(*position, 1.0f));

			const glm::vec3 radians = glm::radians(*rotation);

			const float cy = std::cos(radians.y), sy = std::sin(radians.y);
			const float cx = std::cos(radians.x), sx = std::sin(radians.x);
			const float cz = std::cos(radians.z), sz = std::sin(radians.z);

			return glm::mat4(
				glm::vec4(cy * cz + sy * sx * sz, cx * sz, cy * sx * sz - sy * cz, 0.0f) * size.x,
				glm::vec4(sy * sx * cz - cy * sz, cx * cz, sy * sz + cy * sx * cz, 0.0f) * size.y,
				glm::vec4(sy * cx, -sx, cy * cx, 0.0f) * size.z,
				glm::vec4(*position, 1.0f));
		};
	};
};

#pragma once

#include "RenderMath.h"
#include "Serial.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"


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
		Serial<glm::vec3> position;
		Serial<glm::vec3> rotation;
		Serial<glm::vec3> scale = glm::vec3(1.0f);

		// Scale, then rotate, then translate.
		glm::mat4 Matrix() const
		{
			const glm::vec3 radians = glm::radians(*rotation);

			glm::mat4 matrix = glm::translate(glm::mat4(1.0f), *position);
			matrix = glm::rotate(matrix, radians.y, Y_AXIS);
			matrix = glm::rotate(matrix, radians.x, X_AXIS);
			matrix = glm::rotate(matrix, radians.z, Z_AXIS);

			return glm::scale(matrix, *scale);
		};
	};
};

#pragma once

#include "Serial.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"


namespace Loom
{
	/**
	* Loom::Transform
	* - Where a GameObject sits relative to its parent
	* - Rotation is Euler angles in degrees, which is how the inspector edits it
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
			const glm::mat4 translated = glm::translate(glm::mat4(1.0f), *position);
			const glm::mat4 rotated = translated * glm::mat4_cast(glm::quat(glm::radians(*rotation)));

			return glm::scale(rotated, *scale);
		};
	};
};

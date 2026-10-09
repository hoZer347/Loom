#pragma once

#include "glm/glm.hpp"

#include <vector>


namespace Loom
{
	struct Collider;
	struct GameObject;
	struct Rigidbody;

	/**
	* Loom::ContactPoint
	* - One point where two colliders touch, its normal pointing out of the
	*   other collider toward the one being told, the way it is pushed
	*/
	struct ContactPoint final
	{
		glm::vec3 point{ 0.0f };
		glm::vec3 normal{ 0.0f };

		// Negative while they overlap.
		float separation = 0.0f;
	};

	/**
	* Loom::Collision
	* - What OnCollisionEnter, OnCollisionStay and OnCollisionExit are told
	*   about the other side of a contact; the contacts are empty on exit
	*/
	struct Collision final
	{
		Collider* collider = nullptr;
		GameObject* gameObject = nullptr;

		// Null when the other side is a static collider.
		Rigidbody* rigidbody = nullptr;

		std::vector<ContactPoint> contacts{ };

		// The other side's velocity relative to this one's, where they touch.
		glm::vec3 relativeVelocity{ 0.0f };

		// What this step's solver pushed them apart with, summed over the contacts.
		glm::vec3 impulse{ 0.0f };
	};
};

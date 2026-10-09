#pragma once

#include "Loom API.h"

#include "Component.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"


namespace Loom
{
	// How AddForce and AddTorque apply what they are given, as Unity's do.
	enum class ForceMode
	{
		// Newtons over the next physics step, mass taken into account.
		Force,
		// Over the next physics step, mass ignored.
		Acceleration,
		// At once, mass taken into account.
		Impulse,
		// At once, mass ignored.
		VelocityChange,
	};

	/**
	* Loom::Rigidbody
	* - Puts its GameObject under the physics simulation: gravity and forces
	*   move it, and the colliders on the same GameObject push it off every
	*   other collider in its scene, writing where it ends up back into its
	*   transform every physics step
	* - A kinematic one is moved by its velocity alone, pushing others aside
	*   without being pushed back
	* - Its mass spins about its GameObject's origin, spread as its first
	*   box or sphere collider is. A MeshCollider only collides on a
	*   kinematic Rigidbody or none, as in Unity, since its triangles enclose
	*   no volume to spin
	* - Bounciness and friction are a material's in Unity; here they are the
	*   body's, and a static collider takes on whatever touches it
	* - Comes to rest asleep once it has barely moved for a while, skipping
	*   the simulation until something touches it or a force is added
	*/
	struct LOOM_API Rigidbody : Component<Rigidbody, "Physics">
	{
		// Joins the simulation of the scene being stepped.
		void OnPhysics() override;

		void AddForce(const glm::vec3& force, ForceMode mode = ForceMode::Force);
		void AddTorque(const glm::vec3& torque, ForceMode mode = ForceMode::Force);

		bool IsSleeping() const { return m_sleeping; };
		void WakeUp();
		void Sleep();

		// World space, metres and radians a second. Setting either on a
		// sleeping body wakes it at its next step.
		glm::vec3 velocity{ 0.0f };
		glm::vec3 angularVelocity{ 0.0f };

		Serial<float> mass = 1.0f;
		// How quickly the velocity dies away, a second.
		Serial<float> drag = 0.0f;
		Serial<float> angularDrag = 0.05f;
		Serial<bool> useGravity = true;
		Serial<bool> isKinematic = false;
		// 0 stops dead, 1 bounces back as fast as it came.
		Serial<float> bounciness = 0.0f;
		Serial<float> friction = 0.6f;

	private:
		friend struct Simulation;

		// What AddForce and AddTorque have gathered for the next step: an
		// acceleration, a torque, and an angular acceleration.
		glm::vec3 m_force{ 0.0f };
		glm::vec3 m_torque{ 0.0f };
		glm::vec3 m_spin{ 0.0f };

		bool m_sleeping = false;

		// How long it has been nearly still.
		float m_still = 0.0f;

		// Spin inertia's inverse about the body's own axes.
		glm::vec3 m_inverseInertia{ 0.0f };
	};
};

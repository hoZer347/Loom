#include "Rigidbody.h"

#include "Simulation.h"

#include "glm/gtc/quaternion.hpp"


namespace Loom
{
	void Rigidbody::OnPhysics()
	{
		if (Simulation::current)
			Simulation::current->Add(*this);
	};

	void Rigidbody::AddForce(const glm::vec3& force, ForceMode mode)
	{
		const float inverseMass = mass > 0.0f ? 1.0f / mass : 0.0f;

		switch (mode)
		{
		case ForceMode::Force:			m_force += force * inverseMass;		break;
		case ForceMode::Acceleration:	m_force += force;					break;
		case ForceMode::Impulse:		velocity += force * inverseMass;	break;
		case ForceMode::VelocityChange:	velocity += force;					break;
		};

		WakeUp();
	};

	void Rigidbody::AddTorque(const glm::vec3& torque, ForceMode mode)
	{
		switch (mode)
		{
		case ForceMode::Force:			m_torque += torque;		break;
		case ForceMode::Acceleration:	m_spin += torque;		break;
		case ForceMode::VelocityChange:	angularVelocity += torque;	break;

		case ForceMode::Impulse:
		{
			// Through the inertia the last step found, turned the way the body
			// faces now.
			const glm::mat3 turned = glm::mat3_cast(glm::quat_cast(glm::mat3(m_gameObject->WorldMatrix())));
			const glm::vec3 own = glm::transpose(turned) * torque;

			angularVelocity += turned * (own * m_inverseInertia);
			break;
		};
		};

		WakeUp();
	};

	void Rigidbody::WakeUp()
	{
		m_sleeping = false;
		m_still = 0.0f;
	};

	void Rigidbody::Sleep()
	{
		m_sleeping = true;
		velocity = glm::vec3(0.0f);
		angularVelocity = glm::vec3(0.0f);
	};
};

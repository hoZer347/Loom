#pragma once

#include "Component.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <cmath>


namespace AudioScripts
{
	// Circles its GameObject around a centre, level, so an AudioSource on it
	// passes in front of the listener, round one side and back behind.
	struct Orbit : Loom::Component<Orbit>
	{
		void OnUpdate() override
		{
			*m_angle += glm::two_pi<float>() * Loom::Time::DeltaTime() / *m_period;

			glm::vec3& position = *m_gameObject->transform.position;
			position = *m_center + *m_radius * glm::vec3(std::sin(*m_angle), 0.0f, std::cos(*m_angle));
		};

	private:
		Loom::Serial<glm::vec3> m_center;
		Loom::Serial<float> m_radius = 4.0f;
		// Seconds a lap.
		Loom::Serial<float> m_period = 6.0f;
		// Radians.
		Loom::Serial<float> m_angle;
	};
};

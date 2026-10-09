#pragma once

#include "Component.h"

#include "glm/glm.hpp"

#include <cmath>


namespace RaycastsScripts
{
	// Slides its GameObject back and forth along X, so the collider on it
	// moves through the scene's collider tree while the laser looks for it.
	struct Slider : Loom::Component<Slider>
	{
		void OnUpdate() override
		{
			*m_phase += *m_speed;

			glm::vec3& position = *m_gameObject->transform.position;
			position.x = *m_middle + std::sin(*m_phase) * *m_reach;
		};

	private:
		Loom::Serial<float> m_middle;
		Loom::Serial<float> m_reach = 2.0f;
		// Radians a frame.
		Loom::Serial<float> m_speed = 0.01f;
		Loom::Serial<float> m_phase;
	};
};

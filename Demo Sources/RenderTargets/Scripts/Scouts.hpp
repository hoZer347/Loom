#pragma once

#include "Component.h"
#include "GameObject.h"
#include "Mesh.h"
#include "RenderTarget.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <cmath>
#include <vector>


namespace RenderTargetsScripts
{
	// The demo's scouts, the GameObjects under this one, each wandering a
	// figure of its own about where it started. Every frame sight is drawn
	// where each scout is into visible, cleared first, so it holds what they
	// see now, and into explored, never cleared, so it holds everywhere they
	// have seen. Both lie over area from above, its least X and Z then its
	// greatest, which is how the ground's shader reads them.
	struct Scouts : Loom::Component<Scouts>
	{
		void OnUpdate() override
		{
			if (visible == nullptr || explored == nullptr || sight == nullptr)
				return;

			m_time += Loom::Time::DeltaTime();

			const std::vector<Loom::GameObject*>& scouts = m_gameObject->GetChildren();

			while (m_starts.size() < scouts.size())
				m_starts.push_back(scouts[m_starts.size()]->transform.position);

			std::vector<glm::mat4> seen;

			for (size_t i = 0; i < scouts.size(); i++)
			{
				const float nth = (float)i;
				const glm::vec3 offset(std::sin(m_time * ACROSS_SPEED + nth), 0.0f, std::sin(m_time * (DOWN_SPEED + DOWN_SPEED_STEP * nth)));

				scouts[i]->transform.position = m_starts[i] + offset * *wander;

				seen.push_back(glm::scale(glm::translate(glm::mat4(1.0f), *scouts[i]->transform.position), glm::vec3(radius)));
			};

			const glm::mat4 overhead = Loom::RenderTarget::Overhead(glm::vec2(area->x, area->y), glm::vec2(area->z, area->w));
			Loom::Mesh* disc = sight;

			visible->Clear();
			visible->Draw(*disc, seen, overhead, Loom::Blend::Max);
			explored->Draw(*disc, seen, overhead, Loom::Blend::Max);
		};

		Loom::Serial<Loom::RenderTarget*> visible;
		Loom::Serial<Loom::RenderTarget*> explored;

		// Drawn where each scout is, scaled to radius.
		Loom::Serial<Loom::Mesh*> sight;
		Loom::Serial<float> radius = 2.0f;

		// How far a scout strays from where it started.
		Loom::Serial<float> wander = 3.0f;

		Loom::Serial<glm::vec4> area = glm::vec4(-8.0f, -8.0f, 8.0f, 8.0f);

	private:
		// A scout goes across and back at one speed and up and down at another,
		// in radians a second, each up and down a little faster than the last.
		static constexpr float ACROSS_SPEED = 0.5f;
		static constexpr float DOWN_SPEED = 0.7f;
		static constexpr float DOWN_SPEED_STEP = 0.15f;

		std::vector<glm::vec3> m_starts;
		float m_time = 0.0f;
	};
};

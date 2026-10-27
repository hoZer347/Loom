#pragma once

#include "Audio.h"
#include "Component.h"
#include "Shape.hpp"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <string>


namespace AudioScripts
{
	// Rings a clip from where its GameObject stands every interval seconds,
	// first after delay, through Loom::Audio::PlayAt, and lights its Shape up
	// while the note is fresh.
	struct Chime : Loom::Component<Chime>
	{
		void OnUpdate() override
		{
			const float delta = Loom::Time::DeltaTime();

			m_glow -= delta;
			*m_delay -= delta;

			if (*m_delay <= 0.0f)
			{
				*m_delay += *m_interval;
				m_glow = GLOW_SECONDS;

				Loom::Audio::PlayAt(m_clip, m_gameObject->WorldMatrix()[3], m_volume);
			};

			if (m_glow <= 0.0f)
				return;

			if (Shape* shape = m_gameObject->GetComponent<Shape>())
				shape->Tint(m_glowColor);
		};

	private:
		static constexpr float GLOW_SECONDS = 0.3f;

		Loom::Serial<Loom::SoundAsset> m_clip = "Assets/Bell.wav";
		// A sound falls to a tenth of its volume ten units from the listener,
		// so a bell across the room wants more than 1.
		Loom::Serial<float> m_volume = 1.0f;
		Loom::Serial<float> m_interval = 2.0f;
		Loom::Serial<float> m_delay = 1.0f;
		Loom::Serial<glm::vec3> m_glowColor = glm::vec3(1.0f, 0.9f, 0.4f);

		float m_glow = 0.0f;
	};
};

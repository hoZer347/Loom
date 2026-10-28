#include "Clock.h"

#include "DialogueInput.h"
#include "StateMachine.h"
#include "StaticStateMachine.h"

#include "OpenGL.h"

#include <algorithm>
#include <cmath>


namespace Loom
{
	static float s_deltaTime = 0.0f;
	static float s_unscaledDeltaTime = 0.0f;
	static float s_sinceStart = 0.0f;
	static float s_scale = 1.0f;
	static float s_maxDelta = 0.1f;
	static double s_last = 0.0;
	static bool s_started = false;
	static unsigned long long s_frames = 0;

	float Time::DeltaTime() { return s_deltaTime; };

	float Time::UnscaledDeltaTime() { return s_unscaledDeltaTime; };

	float Time::SinceStart() { return s_sinceStart; };

	unsigned long long Time::FrameCount() { return s_frames; };

	void Time::SetScale(float scale) { s_scale = scale > 0.0f && std::isfinite(scale) ? scale : 0.0f; };

	float Time::GetScale() { return s_scale; };

	void Time::SetMaxDelta(float seconds) { s_maxDelta = seconds > 0.0f ? seconds : 0.0f; };

	void Time::Tick()
	{
		const double now = glfwGetTime();

		if (!s_started)
		{
			s_started = true;
			s_last = now;
			s_deltaTime = 0.0f;
			s_unscaledDeltaTime = 0.0f;

			return;
		};

		float raw = float(now - s_last);
		s_last = now;

		// Clamped so a debugger pause does not move a character a metre.
		raw = std::clamp(raw, 0.0f, s_maxDelta);

		s_unscaledDeltaTime = raw;
		s_deltaTime = raw * s_scale;
		s_sinceStart += s_deltaTime;

		s_frames++;
	};

	void Utilities::Tick()
	{
		Time::Tick();
		DialogueInput::Tick();
		StaticStateMachine::Pump();
	};

	void Utilities::Update()
	{
		StateMachineBase::StepAttached();
	};
};

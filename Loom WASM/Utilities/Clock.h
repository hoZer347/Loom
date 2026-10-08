#pragma once

#include "Loom API.h"


namespace Loom
{
	/// The frame clock everything in this library measures against.
	///
	/// One clock, ticked once per frame by the Engine, so that Duration, SpeedManager, the
	/// dialogue stream and the sprite flipbook all advance by the same delta instead of
	/// each keeping its own and drifting apart.
	struct LOOM_API Time final
	{
		/// Seconds the last frame took. Clamped, so a breakpoint or a stalled tab does not
		/// teleport everything that integrates against it.
		static float DeltaTime();

		/// Seconds since the first tick -- the clock the state history is stamped against
		/// and the one a sprite's animation is played from.
		static float SinceStart();

		/// Frames ticked so far. Cheap way for anything to ask "have I already run this frame".
		static unsigned long long FrameCount();

		/// Multiplies DeltaTime. Zero freezes everything driven by this clock.
		static void SetScale(float scale);
		static float GetScale();

		/// The longest a single frame is allowed to report, in seconds. Default 1/10.
		static void SetMaxDelta(float seconds);

	private:
		friend struct Utilities;

		static void Tick();
	};

	/// The per-frame pump for this library: the clock, the edge-detected input, and the
	/// state machines.
	struct Utilities final
	{
		/// The clock, the input and the StaticStateMachine. Loom::Engine calls this first
		/// thing every frame.
		static void Tick();

		/// The state machines on GameObjects. Loom::Engine calls this after the scenes,
		/// on the frames they update.
		static void Update();

		Utilities() = delete;
		~Utilities() = delete;
	};
};

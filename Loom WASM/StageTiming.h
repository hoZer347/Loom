#pragma once

#include "Loom API.h"

#ifndef LOOM_FINAL

#include "Profiler.h"

#include <vector>


namespace Loom
{
	/**
	* Loom::StageTiming
	* - Draws what the Profiler recorded into the ImGui window being drawn
	* - Not in a final build, which has no Profiler to draw from
	*/
	struct LOOM_API StageTiming final
	{
		// Where a row sits across its thread's lanes, from 0 to 1.
		struct Share
		{
			double from = 0.0;
			double to = 0.0;
		};

		// One for each row: the stages share the whole width by their
		// averages, and each one's parts fill its span from the left at the
		// same scale, leaving what it spent outside them.
		static std::vector<Share> Shares(const std::vector<Profiler::Row>& rows);

		// An average frame of the window: for each thread a lane for each
		// depth of its stages, laid out by their Shares.
		static void Lanes();

		// A switch to record every frame rather than one a poll, the Lanes,
		// then for each thread its stages stacked over the trend and each
		// one's average and peak.
		static void Details();

		// A window of the Lanes over the top left of the game, recording only
		// while it is open: collapsed, the Profiler is off and only its title
		// bar is drawn.
		static void Overlay();
	};
};

#endif

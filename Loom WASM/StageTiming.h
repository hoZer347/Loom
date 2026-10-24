#pragma once

#include "Loom API.h"

#ifndef LOOM_FINAL

namespace Loom
{
	/**
	* Loom::StageTiming
	* - Draws what the Profiler recorded into the ImGui window being drawn
	* - Not in a final build, which has no Profiler to draw from
	*/
	struct LOOM_API StageTiming final
	{
		// An average frame of the window laid out in time: for each thread a
		// lane for each depth of its stages.
		static void Lanes();

		// The Lanes, then for each thread its stages stacked over the trend
		// and each one's average and peak.
		static void Details();

		// A window of the Lanes over the top left of the game, recording only
		// while it is open: collapsed, the Profiler is off and only its title
		// bar is drawn.
		static void Overlay();
	};
};

#endif

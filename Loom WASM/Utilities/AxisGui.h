#pragma once

#include "Loom API.h"


namespace Loom
{
	/// Vector fields drawn a drag box per component, each edged in its axis colour:
	/// X red, Y green, Z blue, the same order a colour's R, G and B take.
	struct LOOM_API AxisGui final
	{
		/// Up to four components. A fourth has no axis, so it is left plain.
		static bool DragFloatN(
			const char* label,
			float* values,
			int count,
			float speed = 1.0f,
			const char* format = "%.3f");

		/// As above, with each axis that mixed marks drawn as "-", for a field
		/// several objects disagree on. mixed may be null.
		static bool DragFloatN(
			const char* label,
			float* values,
			int count,
			const bool* mixed,
			float speed,
			const char* format);

		AxisGui() = delete;
	};
};

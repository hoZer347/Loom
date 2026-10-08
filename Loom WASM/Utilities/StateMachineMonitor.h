#pragma once

#include "Loom API.h"


namespace Loom
{
	struct StateMachineBase;
	struct StateReference;

	/// The machine windows and the inspector, drawn with ImGui over the live game, which
	/// is the
	/// better half of the deal anyway: this reads the running machines rather than the
	/// serialized ones. A machine draws its own rows through its component's OnGui, and
	/// Draw() is the whole-game window for finding a machine that is not in front of you.
	struct LOOM_API StateMachineMonitor final
	{
		/// One ImGui window listing every live machine. Call it from a frame that is
		/// already inside ImGui::NewFrame -- Loom's Engine draws its scene GUI from
		/// RenderImGui, which is the natural place.
		static void Draw(bool* open = nullptr);

		/// One machine's rows, without a window around them. What a machine's own OnGui
		/// draws. Its current state is not among them: a machine on a GameObject has
		/// that as a field.
		static void DrawInline(StateMachineBase& machine);

		/// A dropdown of every registered state, which also takes a state's header
		/// dragged out of the Project panel. Whether it picked another state.
		static bool DrawStateField(StateReference& reference);

		StateMachineMonitor() = delete;
	};
};

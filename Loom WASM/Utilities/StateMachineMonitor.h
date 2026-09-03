#pragma once


namespace Loom
{
	struct StateMachineBase;

	/// The machine windows and the inspector, drawn with ImGui over the live game, which
	/// is the
	/// better half of the deal anyway: this reads the running machines rather than the
	/// serialized ones. A machine draws its own rows through its component's OnGui, and
	/// Draw() is the whole-game window for finding a machine that is not in front of you.
	struct StateMachineMonitor final
	{
		/// One ImGui window listing every live machine. Call it from a frame that is
		/// already inside ImGui::NewFrame -- Loom's Engine draws its scene GUI from
		/// RenderImGui, which is the natural place.
		static void Draw(bool* open = nullptr);

		/// One machine's rows, without a window around them. What a machine's own OnGui
		/// draws.
		static void DrawInline(StateMachineBase& machine);

		StateMachineMonitor() = delete;
	};
};

#pragma once


namespace Loom
{
	/// What "carry on" is, on a controller.
	///
	/// It lives here rather than in either state because a line is advanced in TWO places
	/// and they are easy to forget about separately: St_Dg_WaitForInput when the line is
	/// finished and sitting there, and St_Dg_StreamPlainText while it is still typing
	/// itself out, where the same press means "stop teasing and show me the rest". Wire a
	/// new device into only one of them and the box reads as broken exactly half the time
	/// -- held down it works, tapped during the stream it does nothing -- which is a worse
	/// bug than no controller support at all, because it looks like dropped input.
	///
	/// GLFW reports only the key's current level, never the press itself, so the edge is
	/// found here once per frame by Tick.
	struct DialogueInput final
	{
		/// Whether a lettered face button went down this frame -- A, B, X or Y, whatever
		/// the pad calls them. GLFW names them by position, so the same four buttons are
		/// meant on every controller.
		///
		/// All four, deliberately. This is "carry on", not a choice, and nobody holding a
		/// pad reads a box of text and then hunts for which button was nominated -- every
		/// one that falls under the thumb has to work, which is the same reasoning that
		/// already has both the mouse and the spacebar doing it.
		///
		/// The face buttons only. The shoulders and the stick clicks are not where a thumb
		/// rests, and Start belongs to a pause menu -- spending it on dismissing a line
		/// would mean the pause button sometimes does not pause.
		static bool Continued();

		/// The same question of a keyboard: the spacebar, or any of 1, 2 and 3.
		///
		/// The digits are here for the same reason all four face buttons are. A player
		/// reading a line in a game played with three keys has their fingers on 1, 2 and 3
		/// and no reason to think the box wants a different key; pressing one and having
		/// nothing happen reads as the game being stuck. The numpad too, because a key
		/// badge says "1" and does not say which one.
		static bool ContinuedOnKeyboard();

		/// Either hand. What both dialogue states ask.
		static bool ContinuedAnyhow();

		/// A left click this frame. Asked only by the state that waits on a finished line:
		/// a click during the stream is how a box gets dragged around.
		static bool Clicked();

	private:
		friend struct Utilities;

		/// Samples every key and button once, and works out which of them went down since
		/// the last frame. Driven by Utilities::Tick.
		static void Tick();
	};
};

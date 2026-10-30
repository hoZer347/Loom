#pragma once

#include <algorithm>
#include <utility>


namespace Loom
{
	/**
	* Loom::EditorUpdate
	* - What a change merged to the engine or the project asks of an editor
	*   left open, held until it can run: its scripts built again, or a restart
	*   on the executable on disk
	* - Nothing runs through a run of the game or while the editor is busy, and
	*   a restart waits for everything to be saved, as it saves nothing itself.
	*   The scripts' build keeps unsaved work, so it does not wait for that
	*/
	struct EditorUpdate final
	{
		// The larger comes last, since a restart builds the scripts too.
		enum struct Kind { None, Scripts, Restart };

		// Keeps the larger of it and what already waits.
		void Request(Kind kind) { m_waiting = std::max(m_waiting, kind); };

		Kind Waiting() const { return m_waiting; };

		// Called once a frame: hands over what waits once it can run.
		Kind Due(bool playing, bool busy, bool unsaved)
		{
			if (playing || busy || (m_waiting == Kind::Restart && unsaved))
				return Kind::None;

			return std::exchange(m_waiting, Kind::None);
		};

	private:
		Kind m_waiting = Kind::None;
	};
};

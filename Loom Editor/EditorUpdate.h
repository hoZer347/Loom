#pragma once

#include <algorithm>
#include <utility>


namespace Loom
{
	/**
	* Loom::EditorUpdate
	* - What a change merged to the engine or the project asks of an editor
	*   left open, held only while the editor is busy: its scripts built again,
	*   or a restart on the executable on disk
	* - The editor stops any run for either, saves for a restart and takes back
	*   any question about saving, so neither waits for the user
	*/
	struct EditorUpdate final
	{
		// The larger comes last, since a restart builds the scripts too.
		enum class Kind { None, Scripts, Restart };

		// Keeps the larger of it and what already waits.
		void Request(Kind kind) { m_waiting = std::max(m_waiting, kind); };

		Kind Waiting() const { return m_waiting; };

		// Called once a frame: hands over what waits once it can run.
		Kind Due(bool busy)
		{
			return busy ? Kind::None : std::exchange(m_waiting, Kind::None);
		};

	private:
		Kind m_waiting = Kind::None;
	};
};

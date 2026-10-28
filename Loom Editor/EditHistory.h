#pragma once

#include <cstddef>
#include <vector>


namespace Loom
{
	/**
	* Loom::EditHistory
	* - Undo and redo over a value a panel edits whole and saves to a file,
	*   such as its settings, by copies of it either side of each edit
	* - Knows the value last saved, so the panel can mark itself unsaved
	*   until Ctrl+S, and undoing back to it marks it saved again
	*/
	template<typename T>
	struct EditHistory final
	{
		static constexpr size_t max_steps = 100;

		// value as it is in its file, with nothing to undo.
		void Reset(const T& value)
		{
			m_saved = value;
			m_kept = value;
			m_undo.clear();
			m_redo.clear();
		};

		// One step to undo, from the value last kept to value. Nothing when
		// they are the same.
		void Keep(const T& value)
		{
			if (value == m_kept)
				return;

			m_undo.push_back(m_kept);
			m_redo.clear();
			m_kept = value;

			if (m_undo.size() > max_steps)
				m_undo.erase(m_undo.begin());
		};

		// Takes value a step back, or forward again. Whether there was one.
		bool Step(T& value, bool undo)
		{
			std::vector<T>& from = undo ? m_undo : m_redo;
			std::vector<T>& to = undo ? m_redo : m_undo;

			if (from.empty())
				return false;

			to.push_back(m_kept);
			m_kept = from.back();
			from.pop_back();
			value = m_kept;

			return true;
		};

		// value in place of the one last kept, as no step of its own, and as
		// saved when that was, for a change the user did not make.
		void Amend(const T& value)
		{
			if (m_kept == m_saved)
				m_saved = value;

			m_kept = value;
		};

		void Saved(const T& value) { m_saved = value; };

		bool Unsaved(const T& value) const { return !(value == m_saved); };

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

	private:
		T m_saved{ };
		T m_kept{ };

		std::vector<T> m_undo{ };
		std::vector<T> m_redo{ };
	};
};

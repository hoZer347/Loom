#pragma once

#include "UndoHistory.h"


namespace Loom
{
	/**
	* Loom::EditHistory
	* - An UndoHistory over a value a panel edits whole and saves to a file,
	*   such as its settings, kept by copies of it either side of each edit
	* - Knows the value last saved, so the panel can mark itself unsaved
	*   until Ctrl+S, and an edit or undo back to it marks it saved again
	*/
	template<typename T>
	struct EditHistory final
	{
		// value as it is in its file, with nothing to undo.
		void Reset(const T& value)
		{
			m_saved = value;
			m_kept = value;
			m_steps.Clear();
		};

		// One step to undo, from the value last kept to value. Nothing when
		// they are the same.
		void Keep(const T& value)
		{
			if (value == m_kept)
				return;

			m_steps.Push(m_kept);
			m_kept = value;
		};

		// Takes value a step back, or forward again. Whether there was one.
		bool Step(T& value, bool undo)
		{
			if (!m_steps.Step(m_kept, undo))
				return false;

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

		// Every copy, saved, kept and to step to, through change, for a change
		// the user did not make that every step should carry.
		template<typename Change>
		void Rewrite(const Change& change)
		{
			m_saved = change(m_saved);
			m_kept = change(m_kept);
			m_steps.Rewrite(change);
		};

		void Saved(const T& value) { m_saved = value; };

		bool Unsaved(const T& value) const { return !(value == m_saved); };

		bool CanUndo() const { return m_steps.CanUndo(); };
		bool CanRedo() const { return m_steps.CanRedo(); };

	private:
		T m_saved{ };
		T m_kept{ };

		UndoHistory<T> m_steps{ };
	};
};

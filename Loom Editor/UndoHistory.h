#pragma once

#include <cstddef>
#include <optional>
#include <vector>


namespace Loom
{
	/**
	* Loom::UndoHistory
	* - The undo and redo steps a panel keeps of what it edits, each a copy of
	*   it as it stood on the other side of an edit
	* - Counts the steps, so the panel knows whether it stands where it was
	*   last saved: undoing back there marks it saved again, and a new step
	*   made once it has undone past there leaves the save out of reach
	*/
	template<typename T>
	struct UndoHistory final
	{
		static constexpr size_t max_steps = 100;

		// Nothing to undo, as it stands in its file.
		void Clear()
		{
			m_undo.clear();
			m_redo.clear();
			m_steps = 0;
			m_savedAt = 0;
		};

		// before, as it stood before an edit, one step to undo. Only the
		// newest limit steps are kept.
		void Push(T before, size_t limit = max_steps)
		{
			if (m_savedAt > m_steps)
				m_savedAt.reset();

			m_undo.push_back(std::move(before));
			m_redo.clear();
			m_steps++;

			if (m_undo.size() > limit)
				m_undo.erase(m_undo.begin(), m_undo.end() - (ptrdiff_t)limit);
		};

		// Takes back the last Push, for an edit that changed nothing.
		void Pop()
		{
			if (m_undo.empty())
				return;

			m_undo.pop_back();
			m_steps--;
		};

		// The newest step to undo, or none.
		const T* Last() const { return m_undo.empty() ? nullptr : &m_undo.back(); };

		// What Step would hand back, or none.
		const T* Next(bool undo) const
		{
			const std::vector<T>& from = undo ? m_undo : m_redo;

			return from.empty() ? nullptr : &from.back();
		};

		// Swaps value for what it was a step back, or forward again, keeping
		// value to step to the other way. Whether there was one; value is
		// left alone when there was not.
		bool Step(T& value, bool undo)
		{
			std::vector<T>& from = undo ? m_undo : m_redo;
			std::vector<T>& to = undo ? m_redo : m_undo;

			if (from.empty())
				return false;

			to.push_back(std::move(value));
			value = std::move(from.back());
			from.pop_back();
			m_steps = undo ? m_steps - 1 : m_steps + 1;

			return true;
		};

		// Every step through change, for a change the user did not make that
		// every step should carry.
		template<typename Change>
		void Rewrite(const Change& change)
		{
			for (std::vector<T>* steps : { &m_undo, &m_redo })
				for (T& value : *steps)
					value = change(value);
		};

		void Saved() { m_savedAt = m_steps; };
		bool Unsaved() const { return m_savedAt != m_steps; };

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

	private:
		std::vector<T> m_undo{ };
		std::vector<T> m_redo{ };

		// How many steps have been made since Clear, less those undone, and
		// how many it was saved at. None once the save is out of reach.
		size_t m_steps = 0;
		std::optional<size_t> m_savedAt = 0;
	};
};

#pragma once

#include "ScriptTests.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>


struct ImGuiMultiSelectIO;

namespace Loom
{
	/**
	* Loom::TestsPanel
	* - The Tests panel: every LOOM_TEST the project's scripts declare, as a
	*   tree of the files that hold them, a filter, and the result of each
	*   case's last run
	* - Rows select as files do in a file explorer, Ctrl and Shift adding to
	*   it. Run takes the selected cases and every case in a selected file, or
	*   every case shown when none is. It runs between frames, through
	*   RunQueued, so the editor can compile stale scripts first
	* - A double click on a case or a failure follows its line
	*/
	struct TestsPanel final
	{
		struct Callbacks
		{
			std::function<void()> create;
			std::function<void(const std::string& file, int line)> follow;
		};

		void Draw(bool* open, const Callbacks& callbacks);

		// Runs the cases a click queued. Outside ImGui's frame, as a case may
		// load scenes and step the engine.
		void RunQueued();
		bool HasQueued() const { return m_queued.has_value(); };

		// The cases the filter shows, in the order they registered.
		std::vector<const ScriptTests::Case*> Shown() const;

		// The cases Run takes.
		std::vector<const ScriptTests::Case*> Chosen() const;

		void SetFilter(const std::string& filter);
		void QueueShown();

		// The last run's result for the case of this name, or null.
		const ScriptTests::Result* ResultOf(const std::string& name) const;

	private:
		static constexpr size_t filter_size = 128;

		// The cases named, or every case whose name holds filter when none are.
		struct Queued
		{
			std::string filter;
			std::set<std::string> names;
		};

		void Select(ImGuiMultiSelectIO* requests);

		char m_filter[filter_size]{ };
		std::optional<Queued> m_queued{ };
		std::map<std::string, ScriptTests::Result> m_results{ };

		// Rows by the key Select knows them by: a file's path or a case's name,
		// each after a prefix that keeps the two apart.
		std::set<std::string> m_selected{ };
		std::vector<std::string> m_rows{ };
	};
};

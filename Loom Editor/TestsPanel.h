#pragma once

#include "ScriptTests.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

		// Runs the cases a click queued, as many as fit in a frame each call,
		// until none are left. Outside ImGui's frame, as a case may load
		// scenes and step the engine.
		void RunQueued();
		bool HasQueued() const { return m_queued.has_value(); };

		// How far the run going on has got. Its total is 0 until it starts.
		struct Progress
		{
			size_t done = 0;
			size_t total = 0;
		};

		std::optional<Progress> RunProgress() const;

		// The last run to finish: the cases it ran, how long they took, and
		// the time of day it ended.
		struct Ran
		{
			size_t cases = 0;
			double milliseconds = 0.0;
			std::string at;
		};

		const std::optional<Ran>& LastRun() const { return m_lastRun; };

		// The cases the filter shows, in the order they registered.
		const std::vector<const ScriptTests::Case*>& Shown();

		// The cases Run takes.
		const std::vector<const ScriptTests::Case*>& Chosen();

		void SetFilter(const std::string& filter);
		// Does nothing while a run is going on.
		void QueueShown();

		// The last run's result for the case of this name, or null.
		const ScriptTests::Result* ResultOf(const std::string& name) const;

	private:
		static constexpr size_t filter_size = 128;

		// The cases named, or every case whose name holds filter, found when
		// the run starts so a case that loads before then is run too.
		struct Queued
		{
			std::string filter;
			std::optional<std::vector<std::string>> names;
			size_t next = 0;
			std::optional<std::chrono::steady_clock::time_point> started{ };
			Ran ran;
		};

		struct File
		{
			std::string path;
			std::string name;
			std::string key;
			int line = 0;
			size_t cases = 0;
			size_t run = 0;
			size_t failed = 0;
			double milliseconds = 0.0;
		};

		struct Row
		{
			enum class Kind { File, Case, Failure };

			Kind kind = Kind::File;
			int depth = 0;
			size_t file = 0;
			const ScriptTests::Case* test = nullptr;
			const ScriptTests::Result* result = nullptr;
			size_t failure = 0;

			// What the selection and the folding know it by, in the view. A
			// failure row's is its case's.
			const std::string* key = nullptr;
		};

		// What the filter shows, laid out as the tree's rows.
		struct View
		{
			std::string filter;
			size_t generation = 0;
			std::vector<const ScriptTests::Case*> shown;
			std::vector<std::string> keys;
			std::vector<size_t> files_of;
			std::vector<const ScriptTests::Result*> results;
			std::vector<File> files;
			std::vector<std::vector<size_t>> cases_of;
			std::vector<Row> rows;
			size_t passed = 0;
			size_t failed = 0;
		};

		void Refresh();
		void Rebuild();
		void LayOut();
		void DrawRow(size_t index, const Callbacks& callbacks);
		void Select(ImGuiMultiSelectIO* requests);
		void Queue(Queued queued);

		char m_filter[filter_size]{ };
		std::optional<Queued> m_queued{ };
		std::optional<Ran> m_lastRun{ };
		std::unordered_map<std::string, ScriptTests::Result> m_results{ };

		std::optional<View> m_view{ };
		std::optional<std::vector<const ScriptTests::Case*>> m_chosen{ };

		// Rows by key: a file's path or a case's name, each after a prefix
		// that keeps the two apart.
		std::unordered_set<std::string> m_selected{ };
		std::unordered_set<std::string> m_folded{ };

		// The results or the folding changed since the view was built.
		bool m_stale = false;
	};
};

#pragma once

#include "ScriptTests.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::TestsPanel
	* - The Tests panel: every LOOM_TEST the project's scripts declare, a
	*   filter, and the result of each case's last run
	* - Run takes every case the filter shows; a row's own button takes that
	*   one. Either runs between frames, through RunQueued, so the editor can
	*   compile stale scripts first
	* - A failure's line is followed by a click on it
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

		void SetFilter(const std::string& filter);
		void QueueShown();

		// The last run's result for the case of this name, or null.
		const ScriptTests::Result* ResultOf(const std::string& name) const;

	private:
		static constexpr size_t filter_size = 128;

		// The case named text, or every case whose name holds it.
		struct Queued
		{
			std::string text;
			bool whole_name = false;
		};

		char m_filter[filter_size]{ };
		std::optional<Queued> m_queued{ };
		std::map<std::string, ScriptTests::Result> m_results{ };
	};
};

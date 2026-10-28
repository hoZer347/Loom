#include "TestsPanel.h"

#include "EditorIcons.h"
#include "EditorTheme.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>


namespace Loom
{
	namespace
	{
		struct Status
		{
			const char* label;
			ImVec4 colour;
		};

		Status StatusOf(ScriptTests::Outcome outcome)
		{
			switch (outcome)
			{
			case ScriptTests::Outcome::Passed:	return { "Passed", EditorTheme::SuccessText };
			case ScriptTests::Outcome::Failed:	return { "Failed", EditorTheme::ErrorText };
			case ScriptTests::Outcome::Threw:	return { "Threw", EditorTheme::ErrorText };
			case ScriptTests::Outcome::Crashed:	return { "Crashed", EditorTheme::ErrorText };
			};

			return { "", EditorTheme::ErrorText };
		};

		std::string FileKey(const std::string& file)
		{
			return "file:" + file;
		};

		std::string CaseKey(const std::string& name)
		{
			return "case:" + name;
		};

		std::string FileName(const std::string& file)
		{
			return file.substr(file.find_last_of("/\\") + 1);
		};

		bool DoubleClicked()
		{
			return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		};

		std::string TimeOfDay()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{ };
			char text[sizeof("00:00:00")]{ };

#ifndef __EMSCRIPTEN__
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			std::strftime(text, sizeof(text), "%H:%M:%S", &local);

			return text;
		};
	};

	#pragma region Drawing

	void TestsPanel::Draw(bool* open, const Callbacks& callbacks)
	{
		if (ImGui::Begin(EditorPanels::Tests, open))
		{
			const std::vector<const ScriptTests::Case*>& chosen = Chosen();
			const View& view = *m_view;

			#pragma region Toolbar

			if (ImGui::Button("New"))
				callbacks.create();

			ImGui::SetItemTooltip("Adds a tests script");
			ImGui::SameLine();
			ImGui::BeginDisabled(chosen.empty() || HasQueued());

			if (ImGui::Button(("Run " + std::to_string(chosen.size()) + "###run").c_str()))
			{
				std::vector<std::string> names;

				for (const ScriptTests::Case* test : chosen)
					names.push_back(test->name);

				Queue({ { }, std::move(names) });
			};

			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Runs the selected tests, or every test shown");

			ImGui::SameLine();
			ImGui::TextColored(EditorTheme::SuccessText, "%zu passed", view.passed);
			ImGui::SameLine();
			ImGui::TextColored(view.failed ? EditorTheme::ErrorText : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%zu failed", view.failed);
			ImGui::SameLine();
			ImGui::TextDisabled("%zu not run", view.shown.size() - view.passed - view.failed);

			if (const std::optional<Progress> progress = RunProgress())
			{
				constexpr float progress_ems = 12.0f;
				const std::string overlay = progress->total ? std::to_string(progress->done) + " / " + std::to_string(progress->total) : "Waiting";

				ImGui::SameLine();
				ImGui::ProgressBar(progress->total ? (float)progress->done / progress->total : 0.0f, ImVec2(ImGui::GetFontSize() * progress_ems, 0.0f), overlay.c_str());
			}
			else if (m_lastRun)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("Ran %zu in %.0f ms at %s", m_lastRun->cases, m_lastRun->milliseconds, m_lastRun->at.c_str());
			};

			ImGui::SameLine();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));

			#pragma endregion

			#pragma region Cases

			constexpr int column_count = 3;
			constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
			constexpr ImGuiMultiSelectFlags select_flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid;

			if (ImGui::BeginTable("##cases", column_count, table_flags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				ImGuiMultiSelectIO* const requests = ImGui::BeginMultiSelect(select_flags, (int)m_selected.size(), (int)view.rows.size());
				ImGuiListClipper clipper;

				Select(requests);
				clipper.Begin((int)view.rows.size());

				if (requests->RangeSrcItem != -1)
					clipper.IncludeItemByIndex((int)requests->RangeSrcItem);

				while (clipper.Step())
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
						DrawRow((size_t)row, callbacks);

				Select(ImGui::EndMultiSelect());
				ImGui::EndTable();
			};

			#pragma endregion
		};

		ImGui::End();
	};

	void TestsPanel::DrawRow(size_t index, const Callbacks& callbacks)
	{
		constexpr ImGuiTreeNodeFlags row_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		constexpr ImGuiTreeNodeFlags leaf_flags = row_flags | ImGuiTreeNodeFlags_Leaf;

		const Row& row = m_view->rows[index];
		const float indent = row.depth * ImGui::GetStyle().IndentSpacing;

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::PushID(row.key->c_str());

		if (indent > 0.0f)
			ImGui::Indent(indent);

		if (row.kind == Row::Kind::Failure)
		{
			const ScriptTests::Failure& failure = row.result->failures[row.failure];

			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::ErrorText);
			ImGui::TreeNodeEx((void*)(intptr_t)row.failure, leaf_flags, "%s", failure.text.c_str());
			ImGui::PopStyleColor();
			ImGui::SetItemTooltip("%s:%d", failure.file.c_str(), failure.line);

			if (DoubleClicked())
				callbacks.follow(failure.file, failure.line);
		}
		else
		{
			const bool is_file = row.kind == Row::Kind::File;
			const bool has_children = is_file || (row.result && !row.result->failures.empty());
			const bool folded = m_folded.contains(*row.key);
			const ImGuiTreeNodeFlags flags = (has_children ? row_flags | ImGuiTreeNodeFlags_OpenOnArrow : leaf_flags) | (m_selected.contains(*row.key) ? ImGuiTreeNodeFlags_Selected : 0);
			const File& file = m_view->files[row.file];

			ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)index);

			if (has_children)
				ImGui::SetNextItemOpen(!folded);

			const bool open = ImGui::TreeNodeEx("##row", flags, "%s", is_file ? file.name.c_str() : row.test->name.c_str());

			if (has_children && open == folded)
			{
				if (open)
					m_folded.erase(*row.key);
				else
					m_folded.insert(*row.key);

				m_stale = true;
			};

			if (is_file)
				ImGui::SetItemTooltip("%s", file.path.c_str());
			else
				ImGui::SetItemTooltip("%s:%d", row.test->file.c_str(), row.test->line);

			if (DoubleClicked())
				callbacks.follow(is_file ? file.path : row.test->file, is_file ? file.line : row.test->line);

			ImGui::TableNextColumn();

			if (is_file)
			{
				if (file.failed)
					ImGui::TextColored(EditorTheme::ErrorText, "Failed");
				else if (file.run == file.cases)
					ImGui::TextColored(EditorTheme::SuccessText, "Passed");

				ImGui::TableNextColumn();

				if (file.run)
					ImGui::TextDisabled("%.1f ms", file.milliseconds);
			}
			else if (row.result)
			{
				const auto [label, colour] = StatusOf(row.result->outcome);

				ImGui::TextColored(colour, "%s", label);
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%.1f ms", row.result->milliseconds);
			};
		};

		if (indent > 0.0f)
			ImGui::Unindent(indent);

		ImGui::PopID();
	};

	void TestsPanel::Select(ImGuiMultiSelectIO* requests)
	{
		const std::vector<Row>& rows = m_view->rows;

		for (const ImGuiSelectionRequest& request : requests->Requests)
		{
			if (request.Type == ImGuiSelectionRequestType_SetAll)
			{
				if (request.Selected)
				{
					for (const Row& row : rows)
						if (row.kind != Row::Kind::Failure)
							m_selected.insert(*row.key);
				}
				else
					m_selected.clear();
			}
			else if (request.Type == ImGuiSelectionRequestType_SetRange)
			{
				const size_t first = (size_t)std::min(request.RangeFirstItem, request.RangeLastItem);
				const size_t last = (size_t)std::max(request.RangeFirstItem, request.RangeLastItem);

				for (size_t row = first; row <= last && row < rows.size(); ++row)
					if (rows[row].kind == Row::Kind::Failure)
						continue;
					else if (request.Selected)
						m_selected.insert(*rows[row].key);
					else
						m_selected.erase(*rows[row].key);
			};

			m_chosen.reset();
		};
	};

	#pragma endregion

	#pragma region View

	void TestsPanel::Refresh()
	{
		if (!m_view || m_view->generation != ScriptTests::Generation() || m_view->filter != m_filter)
			Rebuild();
		else if (m_stale)
			LayOut();
	};

	void TestsPanel::Rebuild()
	{
		View& view = m_view.emplace(View{ m_filter, ScriptTests::Generation() });
		std::unordered_map<std::string, size_t> places;

		for (const ScriptTests::Case& test : ScriptTests::All())
		{
			if (!ScriptTests::Matches(test.name, view.filter))
				continue;

			const auto [place, added] = places.try_emplace(test.file, view.files.size());

			if (added)
			{
				view.files.push_back({ test.file, FileName(test.file), FileKey(test.file), test.line });
				view.cases_of.emplace_back();
			};

			view.cases_of[place->second].push_back(view.shown.size());
			view.files_of.push_back(place->second);
			view.shown.push_back(&test);
			view.keys.push_back(CaseKey(test.name));
			++view.files[place->second].cases;
		};

		LayOut();
	};

	void TestsPanel::LayOut()
	{
		View& view = *m_view;

		m_stale = false;
		m_chosen.reset();
		view.results.clear();
		view.rows.clear();
		view.passed = 0;
		view.failed = 0;

		for (File& file : view.files)
		{
			file.run = 0;
			file.failed = 0;
			file.milliseconds = 0.0;
		};

		for (size_t i = 0; i < view.shown.size(); ++i)
		{
			const ScriptTests::Result* result = m_results.empty() ? nullptr : ResultOf(view.shown[i]->name);

			view.results.push_back(result);

			if (result)
			{
				const bool failed = result->outcome != ScriptTests::Outcome::Passed;
				File& file = view.files[view.files_of[i]];

				++file.run;
				file.failed += failed;
				file.milliseconds += result->milliseconds;
				++(failed ? view.failed : view.passed);
			};
		};

		const auto folded = [this](const std::string& key) { return !m_folded.empty() && m_folded.contains(key); };

		for (size_t f = 0; f < view.files.size(); ++f)
		{
			view.rows.push_back({ Row::Kind::File, 0, f, nullptr, nullptr, 0, &view.files[f].key });

			if (folded(view.files[f].key))
				continue;

			for (const size_t shown : view.cases_of[f])
			{
				const ScriptTests::Result* result = view.results[shown];
				const std::string* key = &view.keys[shown];

				view.rows.push_back({ Row::Kind::Case, 1, f, view.shown[shown], result, 0, key });

				if (result && !folded(*key))
					for (size_t i = 0; i < result->failures.size(); ++i)
						view.rows.push_back({ Row::Kind::Failure, 2, f, view.shown[shown], result, i, key });
			};
		};
	};

	const std::vector<const ScriptTests::Case*>& TestsPanel::Shown()
	{
		Refresh();

		return m_view->shown;
	};

	const std::vector<const ScriptTests::Case*>& TestsPanel::Chosen()
	{
		Refresh();

		if (!m_chosen)
		{
			const View& view = *m_view;
			std::vector<const ScriptTests::Case*> chosen;

			if (!m_selected.empty())
				for (size_t i = 0; i < view.shown.size(); ++i)
					if (m_selected.contains(view.keys[i]) || m_selected.contains(view.files[view.files_of[i]].key))
						chosen.push_back(view.shown[i]);

			m_chosen = chosen.empty() ? view.shown : std::move(chosen);
		};

		return *m_chosen;
	};

	#pragma endregion

	#pragma region Running

	void TestsPanel::RunQueued()
	{
		// About half a frame at 60 a second, so the editor keeps drawing while a long run goes on.
		constexpr double step_milliseconds = 8.0;

		if (!m_queued)
			return;

		Queued& queued = *m_queued;

		if (!queued.names)
		{
			queued.names.emplace();

			for (const ScriptTests::Case& test : ScriptTests::All())
				if (ScriptTests::Matches(test.name, queued.filter))
					queued.names->push_back(test.name);
		};

		if (!queued.started)
			queued.started = std::chrono::steady_clock::now();

		const std::vector<std::string>& names = *queued.names;
		const auto start = std::chrono::steady_clock::now();

		while (queued.next < names.size())
		{
			const std::string& name = names[queued.next++];

			if (const ScriptTests::Case* test = ScriptTests::Find(name))
			{
				m_results[name] = ScriptTests::Run(*test);
				m_stale = true;
				++queued.ran.cases;
			};

			if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= step_milliseconds)
				break;
		};

		if (queued.next == names.size())
		{
			queued.ran.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *queued.started).count();
			queued.ran.at = TimeOfDay();
			m_lastRun = std::move(queued.ran);
			m_queued.reset();
		};
	};

	std::optional<TestsPanel::Progress> TestsPanel::RunProgress() const
	{
		if (!m_queued)
			return std::nullopt;

		return Progress{ m_queued->next, m_queued->names ? m_queued->names->size() : 0 };
	};

	void TestsPanel::SetFilter(const std::string& filter)
	{
		const size_t length = std::min(filter.size(), filter_size - 1);

		filter.copy(m_filter, length);
		m_filter[length] = '\0';
	};

	void TestsPanel::QueueShown()
	{
		Queue({ m_filter });
	};

	void TestsPanel::Queue(Queued queued)
	{
		if (!m_queued)
			m_queued = std::move(queued);
	};

	const ScriptTests::Result* TestsPanel::ResultOf(const std::string& name) const
	{
		const auto found = m_results.find(name);

		return found == m_results.end() ? nullptr : &found->second;
	};

	#pragma endregion
};

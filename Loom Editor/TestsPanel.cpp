#include "TestsPanel.h"

#include "EditorIcons.h"
#include "EditorTheme.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
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

		using FileCases = std::pair<std::string, std::vector<const ScriptTests::Case*>>;

		// The cases under the files that hold them, each file where its first case is.
		std::vector<FileCases> ByFile(const std::vector<const ScriptTests::Case*>& cases)
		{
			std::vector<FileCases> files;

			for (const ScriptTests::Case* test : cases)
			{
				auto found = std::ranges::find(files, test->file, &FileCases::first);

				if (found == files.end())
					found = files.insert(files.end(), { test->file, { } });

				found->second.push_back(test);
			};

			return files;
		};

		bool DoubleClicked()
		{
			return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		};
	};

	#pragma region Drawing

	void TestsPanel::Draw(bool* open, const Callbacks& callbacks)
	{
		if (ImGui::Begin(EditorPanels::Tests, open))
		{
			const std::vector<const ScriptTests::Case*> shown = Shown();
			const std::vector<const ScriptTests::Case*> chosen = Chosen();

			#pragma region Toolbar

			if (ImGui::Button("New"))
				callbacks.create();

			ImGui::SetItemTooltip("Adds a tests script");
			ImGui::SameLine();
			ImGui::BeginDisabled(chosen.empty());

			if (ImGui::Button(("Run " + std::to_string(chosen.size()) + "###run").c_str()))
			{
				Queued queued;

				for (const ScriptTests::Case* test : chosen)
					queued.names.insert(test->name);

				m_queued = std::move(queued);
			};

			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Runs the selected tests, or every test shown");

			size_t passed = 0;
			size_t failed = 0;

			for (const ScriptTests::Case* test : shown)
				if (const ScriptTests::Result* result = ResultOf(test->name))
					++(result->outcome == ScriptTests::Outcome::Passed ? passed : failed);

			ImGui::SameLine();
			ImGui::TextColored(EditorTheme::SuccessText, "%zu passed", passed);
			ImGui::SameLine();
			ImGui::TextColored(failed ? EditorTheme::ErrorText : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%zu failed", failed);
			ImGui::SameLine();
			ImGui::TextDisabled("%zu not run", shown.size() - passed - failed);

			ImGui::SameLine();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));

			#pragma endregion

			#pragma region Cases

			constexpr int column_count = 3;
			constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
			constexpr ImGuiTreeNodeFlags row_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
			constexpr ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			constexpr ImGuiMultiSelectFlags select_flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid;

			if (ImGui::BeginTable("##cases", column_count, table_flags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				Select(ImGui::BeginMultiSelect(select_flags, (int)m_selected.size(), (int)m_rows.size()));
				m_rows.clear();

				// A row the selection knows, under the key it keeps.
				const auto selectable =
					[this](const std::string& key, ImGuiTreeNodeFlags flags)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)m_rows.size());
						m_rows.push_back(key);

						return m_selected.contains(key) ? flags | ImGuiTreeNodeFlags_Selected : flags;
					};

				for (const auto& [file, cases] : ByFile(shown))
				{
					ImGui::PushID(file.c_str());

					const bool file_open = ImGui::TreeNodeEx("##file", selectable(FileKey(file), row_flags), "%s", FileName(file).c_str());

					ImGui::SetItemTooltip("%s", file.c_str());

					if (DoubleClicked())
						callbacks.follow(file, cases.front()->line);

					size_t file_failed = 0;
					size_t file_run = 0;
					double file_milliseconds = 0.0;

					for (const ScriptTests::Case* test : cases)
						if (const ScriptTests::Result* result = ResultOf(test->name))
						{
							++file_run;
							file_failed += result->outcome != ScriptTests::Outcome::Passed;
							file_milliseconds += result->milliseconds;
						};

					ImGui::TableNextColumn();

					if (file_failed)
						ImGui::TextColored(EditorTheme::ErrorText, "Failed");
					else if (file_run == cases.size())
						ImGui::TextColored(EditorTheme::SuccessText, "Passed");

					ImGui::TableNextColumn();

					if (file_run)
						ImGui::TextDisabled("%.1f ms", file_milliseconds);

					if (file_open)
					{
						for (const ScriptTests::Case* test : cases)
						{
							const ScriptTests::Result* result = ResultOf(test->name);
							const bool has_failures = result && !result->failures.empty();

							ImGui::PushID(test->name.c_str());

							const bool case_open = ImGui::TreeNodeEx("##case", selectable(CaseKey(test->name), has_failures ? row_flags : row_flags | leaf_flags), "%s", test->name.c_str());

							ImGui::SetItemTooltip("%s:%d", test->file.c_str(), test->line);

							if (DoubleClicked())
								callbacks.follow(test->file, test->line);

							ImGui::TableNextColumn();

							if (result)
							{
								const auto [label, colour] = StatusOf(result->outcome);

								ImGui::TextColored(colour, "%s", label);
								ImGui::TableNextColumn();
								ImGui::TextDisabled("%.1f ms", result->milliseconds);
							};

							if (has_failures && case_open)
							{
								for (size_t i = 0; i < result->failures.size(); ++i)
								{
									const ScriptTests::Failure& failure = result->failures[i];

									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::ErrorText);
									ImGui::TreeNodeEx((void*)(intptr_t)i, ImGuiTreeNodeFlags_SpanAllColumns | leaf_flags, "%s", failure.text.c_str());
									ImGui::PopStyleColor();
									ImGui::SetItemTooltip("%s:%d", failure.file.c_str(), failure.line);

									if (DoubleClicked())
										callbacks.follow(failure.file, failure.line);
								};

								ImGui::TreePop();
							};

							ImGui::PopID();
						};

						ImGui::TreePop();
					};

					ImGui::PopID();
				};

				Select(ImGui::EndMultiSelect());
				ImGui::EndTable();
			};

			#pragma endregion
		};

		ImGui::End();
	};

	void TestsPanel::Select(ImGuiMultiSelectIO* requests)
	{
		for (const ImGuiSelectionRequest& request : requests->Requests)
		{
			if (request.Type == ImGuiSelectionRequestType_SetAll)
			{
				if (request.Selected)
					m_selected.insert(m_rows.begin(), m_rows.end());
				else
					m_selected.clear();
			}
			else if (request.Type == ImGuiSelectionRequestType_SetRange)
			{
				const size_t first = (size_t)std::min(request.RangeFirstItem, request.RangeLastItem);
				const size_t last = (size_t)std::max(request.RangeFirstItem, request.RangeLastItem);

				for (size_t row = first; row <= last && row < m_rows.size(); ++row)
					if (request.Selected)
						m_selected.insert(m_rows[row]);
					else
						m_selected.erase(m_rows[row]);
			};
		};
	};

	#pragma endregion

	#pragma region Running

	void TestsPanel::RunQueued()
	{
		if (!m_queued)
			return;

		const Queued queued = *std::exchange(m_queued, std::nullopt);

		for (const ScriptTests::Case& test : ScriptTests::All())
			if (queued.names.empty() ? ScriptTests::Matches(test.name, queued.filter) : queued.names.contains(test.name))
				m_results[test.name] = ScriptTests::Run(test);
	};

	std::vector<const ScriptTests::Case*> TestsPanel::Shown() const
	{
		std::vector<const ScriptTests::Case*> shown;

		for (const ScriptTests::Case& test : ScriptTests::All())
			if (ScriptTests::Matches(test.name, m_filter))
				shown.push_back(&test);

		return shown;
	};

	std::vector<const ScriptTests::Case*> TestsPanel::Chosen() const
	{
		const std::vector<const ScriptTests::Case*> shown = Shown();
		std::vector<const ScriptTests::Case*> chosen;

		for (const ScriptTests::Case* test : shown)
			if (m_selected.contains(CaseKey(test->name)) || m_selected.contains(FileKey(test->file)))
				chosen.push_back(test);

		return chosen.empty() ? shown : chosen;
	};

	void TestsPanel::SetFilter(const std::string& filter)
	{
		const size_t length = std::min(filter.size(), filter_size - 1);

		filter.copy(m_filter, length);
		m_filter[length] = '\0';
	};

	void TestsPanel::QueueShown()
	{
		m_queued = Queued{ m_filter };
	};

	const ScriptTests::Result* TestsPanel::ResultOf(const std::string& name) const
	{
		const auto found = m_results.find(name);

		return found == m_results.end() ? nullptr : &found->second;
	};

	#pragma endregion
};

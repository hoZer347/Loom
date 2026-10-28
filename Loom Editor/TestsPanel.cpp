#include "TestsPanel.h"

#include "EditorIcons.h"
#include "EditorTheme.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
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
	};

	#pragma region Drawing

	void TestsPanel::Draw(bool* open, const Callbacks& callbacks)
	{
		if (ImGui::Begin(EditorPanels::Tests, open))
		{
			const std::vector<const ScriptTests::Case*> shown = Shown();

			#pragma region Toolbar

			if (ImGui::Button("New"))
				callbacks.create();

			ImGui::SetItemTooltip("Adds a tests script");
			ImGui::SameLine();
			ImGui::BeginDisabled(shown.empty());

			if (ImGui::Button(("Run " + std::to_string(shown.size()) + "###run").c_str()))
				QueueShown();

			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Runs every test shown");

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

			constexpr int column_count = 4;
			constexpr int test_column = 2;
			constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;

			if (ImGui::BeginTable("##cases", column_count, table_flags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("##run", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				for (const ScriptTests::Case* test : shown)
				{
					const ScriptTests::Result* result = ResultOf(test->name);

					ImGui::PushID(test->name.c_str());
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					if (ImGui::SmallButton("Run"))
						m_queued = Queued{ test->name, true };

					ImGui::SetItemTooltip("Runs this test");

					ImGui::TableNextColumn();

					if (result)
					{
						const auto [label, colour] = StatusOf(result->outcome);

						ImGui::TextColored(colour, "%s", label);
					}
					else
						ImGui::TextDisabled("-");

					ImGui::TableNextColumn();

					if (ImGui::Selectable(test->name.c_str()))
						callbacks.follow(test->file, test->line);

					ImGui::SetItemTooltip("%s:%d", test->file.c_str(), test->line);

					ImGui::TableNextColumn();

					if (result)
					{
						ImGui::TextDisabled("%.1f ms", result->milliseconds);

						for (size_t i = 0; i < result->failures.size(); ++i)
						{
							const ScriptTests::Failure& failure = result->failures[i];

							ImGui::PushID((int)i);
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(test_column);
							ImGui::Indent();
							ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::ErrorText);

							if (ImGui::Selectable(failure.text.c_str()))
								callbacks.follow(failure.file, failure.line);

							ImGui::PopStyleColor();
							ImGui::SetItemTooltip("%s:%d", failure.file.c_str(), failure.line);
							ImGui::Unindent();
							ImGui::PopID();
						};
					};

					ImGui::PopID();
				};

				ImGui::EndTable();
			};

			#pragma endregion
		};

		ImGui::End();
	};

	#pragma endregion

	#pragma region Running

	void TestsPanel::RunQueued()
	{
		if (!m_queued)
			return;

		const Queued queued = *std::exchange(m_queued, std::nullopt);

		for (const ScriptTests::Case& test : ScriptTests::All())
			if (queued.whole_name ? test.name == queued.text : ScriptTests::Matches(test.name, queued.text))
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

#include "EditorConsole.h"
#include "EditorLog.h"
#include "EditorTheme.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <string>
#include <utility>


namespace Loom
{
	namespace
	{
		// The byte offset in text whose boundary falls nearest x, measured from
		// where the text starts.
		size_t TextOffsetAt(const std::string& text, float x)
		{
			constexpr float half = 0.5f;

			const char* const begin = text.c_str();
			const char* const end = begin + text.size();
			const char* at = begin;
			float left = 0.0f;

			while (at < end)
			{
				unsigned int c = 0;
				const char* const next = at + ImTextCharFromUtf8(&c, at, end);
				const float width = ImGui::CalcTextSize(at, next).x;

				if (x < left + width * half)
					break;

				left += width;
				at = next;
			};

			return at - begin;
		};
	};

	void EditorConsole::Draw(bool* open)
	{
		if (ImGui::Begin("Console", open))
		{
			if (ImGui::Button("Clear"))
				EditorLog::Get().Clear();

			ImGui::SameLine();
			ImGui::Checkbox("Auto-scroll", &m_autoScroll);

			ImGui::SameLine();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint(
				"##filter",
				"Filter",
				m_filter,
				sizeof(m_filter));

			ImGui::Separator();

			if (ImGui::BeginChild(
				"##lines",
				ImVec2(0.0f, 0.0f),
				ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar))
			{
				const char* filter = m_filter;
				const bool copy = std::exchange(m_copy, false) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C);
				const bool select_all = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A);

				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const ImVec2 visible = ImGui::GetContentRegionAvail();
				const ImVec2 mouse = ImGui::GetMousePos();
				const float line_height = ImGui::GetTextLineHeight();
				const float newline_width = ImGui::CalcTextSize(" ").x;
				const ImU32 highlight = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
				ImDrawList* const draw_list = ImGui::GetWindowDrawList();

				const Position first = std::min(m_anchor, m_caret);
				const Position last = std::max(m_anchor, m_caret);

				bool any = false;
				Position text_start{ }, text_end{ };
				Position under_mouse{ };
				size_t under_mouse_length = 0;
				float text_bottom = origin.y;
				float text_width = visible.x;
				std::string copied;

				EditorLog::Get().ForEach(
					[&](size_t id, const std::string& line, bool is_error)
					{
						if (filter[0] && line.find(filter) == std::string::npos)
							return;

						const ImVec2 at = ImGui::GetCursorScreenPos();
						const Position line_start{ id, 0 };
						const Position line_end{ id, line.size() };

						// Above the first line counts as its start.
						if (!any)
						{
							text_start = line_start;
							under_mouse = line_start;
							under_mouse_length = line.size();
							any = true;
						};

						text_end = line_end;
						text_bottom = at.y + line_height;

						if (mouse.y >= at.y)
						{
							under_mouse = { id, TextOffsetAt(line, mouse.x - at.x) };
							under_mouse_length = line.size();
						};

						if (first < last && line_start <= last && first <= line_end)
						{
							const size_t from = first.line == id ? first.offset : 0;
							const size_t to = last.line == id ? last.offset : line.size();
							const char* const text = line.c_str();

							float right = at.x + ImGui::CalcTextSize(text, text + to).x;

							if (last.line != id)
								right += newline_width;

							draw_list->AddRectFilled(
								ImVec2(at.x + ImGui::CalcTextSize(text, text + from).x, at.y),
								ImVec2(right, at.y + line_height),
								highlight);

							if (copy)
							{
								copied.append(line, from, to - from);

								if (last.line != id)
									copied += '\n';
							};
						};

						if (is_error)
							ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::ErrorText);

						ImGui::TextUnformatted(line.c_str());

						if (is_error)
							ImGui::PopStyleColor();

						text_width = std::max(text_width, ImGui::GetItemRectSize().x);
					});

				if (any)
				{
					if (mouse.y >= text_bottom)
						under_mouse = text_end;

					// One item over all the text, so a drag selects rather than
					// moving the window, and the selection has a context menu.
					ImGui::SetCursorScreenPos(origin);
					ImGui::InvisibleButton("##select", ImVec2(text_width, std::max(text_bottom - origin.y, visible.y)));

					if (ImGui::IsItemHovered())
						ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

					if (ImGui::IsItemActivated())
					{
						if (!ImGui::GetIO().KeyShift)
							m_anchor = under_mouse;

						m_caret = under_mouse;
						m_dragging = true;
					};

					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						m_anchor = { under_mouse.line, 0 };
						m_caret = { under_mouse.line, under_mouse_length };
						m_dragging = false;
					};

					if (!ImGui::IsItemActive())
						m_dragging = false;

					if (m_dragging)
					{
						m_caret = under_mouse;

						// Dragging past an edge scrolls toward it, faster the further out.
						const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;

						if (mouse.y < inner.Min.y)
							ImGui::SetScrollY(ImGui::GetScrollY() - (inner.Min.y - mouse.y));
						else if (mouse.y > inner.Max.y)
							ImGui::SetScrollY(ImGui::GetScrollY() + (mouse.y - inner.Max.y));

						if (mouse.x < inner.Min.x)
							ImGui::SetScrollX(ImGui::GetScrollX() - (inner.Min.x - mouse.x));
						else if (mouse.x > inner.Max.x)
							ImGui::SetScrollX(ImGui::GetScrollX() + (mouse.x - inner.Max.x));
					};

					if (ImGui::BeginPopupContextItem("##console menu"))
					{
						if (ImGui::MenuItem("Copy", "Ctrl+C", false, m_anchor != m_caret))
							m_copy = true;

						if (ImGui::MenuItem("Select All", "Ctrl+A"))
						{
							m_anchor = text_start;
							m_caret = text_end;
						};

						ImGui::EndPopup();
					};

					if (select_all)
					{
						m_anchor = text_start;
						m_caret = text_end;
					};
				};

				if (copy && !copied.empty())
					ImGui::SetClipboardText(copied.c_str());

				if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
					ImGui::SetScrollHereY(1.0f);
			};

			ImGui::EndChild();
		};

		ImGui::End();
	};
};

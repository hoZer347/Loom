#include "TabHistory.h"

#include "imgui.h"
#include "imgui_internal.h"


namespace Loom
{
	namespace
	{
		bool IsPanel(const ImGuiWindow& window)
		{
			constexpr ImGuiWindowFlags not_panels =
				ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_DockNodeHost |
				ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip;

			return (window.Flags & not_panels) == 0;
		};

		bool IsPanelOpen(TabHistory::Tab tab)
		{
			const ImGuiWindow* window = ImGui::FindWindowByID(tab);

			return window != nullptr && window->Active;
		};
	};

	void TabHistory::Update(bool navigate)
	{
		const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;

		if (focused != nullptr && IsPanel(*focused->RootWindow))
			Focus(focused->RootWindow->ID);

		if (!navigate || ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
			return;

		std::optional<Tab> tab;

		if (ImGui::IsMouseClicked(back_button))
			tab = Back(IsPanelOpen);
		else if (ImGui::IsMouseClicked(forward_button))
			tab = Forward(IsPanelOpen);

		// A dock shows the tab holding focus.
		if (tab)
			ImGui::FocusWindow(ImGui::FindWindowByID(*tab));
	};

	void TabHistory::Focus(Tab tab)
	{
		if (!m_tabs.empty() && m_tabs[m_current] == tab)
			return;

		if (!m_tabs.empty())
			m_tabs.resize(m_current + 1);

		m_tabs.push_back(tab);

		if ((int)m_tabs.size() > max_steps)
			m_tabs.erase(m_tabs.begin());

		m_current = (int)m_tabs.size() - 1;
	};

	std::optional<TabHistory::Tab> TabHistory::Back(const IsOpen& is_open)
	{
		return Step(-1, is_open);
	};

	std::optional<TabHistory::Tab> TabHistory::Forward(const IsOpen& is_open)
	{
		return Step(1, is_open);
	};

	std::optional<TabHistory::Tab> TabHistory::Step(int direction, const IsOpen& is_open)
	{
		for (int index = m_current + direction; index >= 0 && index < (int)m_tabs.size(); index += direction)
			if (m_tabs[index] != m_tabs[m_current] && is_open(m_tabs[index]))
			{
				m_current = index;

				return m_tabs[index];
			};

		return std::nullopt;
	};
};

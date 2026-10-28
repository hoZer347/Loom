#include "TornWindows.h"

#include "imgui_internal.h"

#include <algorithm>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		constexpr ImGuiWindowFlags torn_flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar;

		constexpr ImGuiWindowFlags undockable_flags =
			ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDocking;

		// Above and below the text.
		constexpr float padding_sides = 2.0f;

		ImGuiDockNode* RootNode(const ImGuiWindow* window)
		{
			ImGuiDockNode* const node = window->DockId != 0 ? ImGui::DockBuilderGetNode(window->DockId) : nullptr;

			return node != nullptr ? ImGui::DockNodeGetRootNode(node) : nullptr;
		};

		bool InDockSpace(const ImGuiWindow* window)
		{
			const ImGuiDockNode* const root = RootNode(window);

			return root != nullptr && root->IsDockSpace();
		};

		int CountWindows(const ImGuiDockNode* node)
		{
			if (node == nullptr)
				return 0;

			return node->Windows.Size + CountWindows(node->ChildNodes[0]) + CountWindows(node->ChildNodes[1]);
		};
	};

	void TornWindows::Draw(ImVec2 bar_padding, const Bar& bar)
	{
		ImGuiContext& g = *GImGui;

		if ((g.ConfigFlagsCurrFrame & ImGuiConfigFlags_ViewportsEnable) == 0)
			return;

		Tear(g.FontSize + bar_padding.y * padding_sides);

		for (const Torn& torn : m_torn)
		{
			ImGuiWindowClass own;
			own.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;

			ImGui::SetNextWindowClass(&own);
			ImGui::SetNextWindowPos(torn.pos, ImGuiCond_Once);
			ImGui::SetNextWindowSize(torn.size, ImGuiCond_Once);

			// The menu bar starts where the editor's does.
			g.NextWindowData.MenuBarOffsetMinVal = ImVec2(
				ImMax(ImMax(g.Style.WindowPadding.x, g.Style.ItemSpacing.x), g.Style.DisplaySafeAreaPadding.x),
				ImMax(g.Style.DisplaySafeAreaPadding.y - bar_padding.y, 0.0f));

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, bar_padding);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::Begin(torn.name.c_str(), nullptr, torn_flags);
			ImGui::PopStyleVar(3);

			g.NextWindowData.MenuBarOffsetMinVal = ImVec2(0.0f, 0.0f);

			if (ImGui::BeginMenuBar())
			{
				bar(torn);
				ImGui::EndMenuBar();
			};

			ImGui::PopStyleVar();
			ImGui::DockSpace(torn.dockspace);
			ImGui::End();
		};
	};

	void TornWindows::Prune()
	{
		const auto empty = [](const Torn& torn)
		{
			if (CountWindows(ImGui::DockBuilderGetNode(torn.dockspace)) > 0)
				return false;

			ImGui::DockBuilderRemoveNode(torn.dockspace);
			return true;
		};

		m_torn.erase(std::remove_if(m_torn.begin(), m_torn.end(), empty), m_torn.end());
	};

	std::vector<ImGuiWindow*> TornWindows::Docked(const Torn& torn) const
	{
		std::vector<ImGuiWindow*> docked;

		for (ImGuiWindow* window : GImGui->Windows)
			if (const ImGuiDockNode* root = RootNode(window); window->DockNodeAsHost == nullptr && root != nullptr && root->ID == torn.dockspace)
				docked.push_back(window);

		return docked;
	};

	void TornWindows::Tear(float bar_height)
	{
		const ImGuiContext& g = *GImGui;

		// A window still in someone's hands may yet be docked.
		if (g.MovingWindow != nullptr || (g.DragDropActive && g.DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW)))
			return;

		const ImRect editor = ((ImGuiViewportP*)ImGui::GetMainViewport())->GetMainRect();
		std::unordered_map<const ImGuiWindow*, ImGuiID> torn_roots;

		for (ImGuiWindow* window : g.Windows)
		{
			if (!window->WasActive || window->IsFallbackWindow || window->DockNodeAsHost != nullptr || (window->Flags & undockable_flags) != 0 || InDockSpace(window))
				continue;

			const ImGuiWindow* const root = window->RootWindowDockTree;

			if (!root->ViewportOwned && editor.Contains(root->Rect()))
				continue;

			const auto [entry, first] = torn_roots.try_emplace(root, 0);

			if (first)
			{
				Torn& torn = m_torn.emplace_back();
				torn.name = "##Torn" + std::to_string(++m_count);
				torn.dockspace = ImHashStr((torn.name + "Dock").c_str());
				torn.pos = ImVec2(root->Pos.x, root->Pos.y - bar_height);
				torn.size = ImVec2(root->Size.x, root->Size.y + bar_height);

				ImGui::DockBuilderAddNode(torn.dockspace, ImGuiDockNodeFlags_DockSpace);
				entry->second = torn.dockspace;
			};

			ImGui::DockBuilderDockWindow(window->Name, entry->second);
		};
	};
};

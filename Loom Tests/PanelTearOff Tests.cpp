#include "doctest.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include <unordered_map>

namespace
{
	constexpr const char* PANEL = "Tear Off Panel";
	constexpr const char* OTHER = "Tear Off Other";

	constexpr ImVec2 EDITOR_POS = ImVec2(100.0f, 100.0f);
	constexpr ImVec2 EDITOR_SIZE = ImVec2(800.0f, 600.0f);
	constexpr ImVec2 MONITOR_SIZE = ImVec2(1920.0f, 1080.0f);
	constexpr ImVec2 PANEL_POS = ImVec2(300.0f, 300.0f);
	constexpr ImVec2 PANEL_SIZE = ImVec2(200.0f, 150.0f);
	constexpr ImVec2 OFF_EDITOR = ImVec2(1300.0f, 500.0f);
	constexpr ImVec2 BELOW_EDITOR = ImVec2(300.0f, 900.0f);
	constexpr int DRAG_STEPS = 8;

	// ImGui keeps window positions on whole pixels.
	constexpr float PIXEL = 1.0f;

	// An ImGui context set up as the editor's, docking and viewports on, over
	// a platform that only remembers where each window is.
	struct Driven
	{
		Driven()
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();
			windows.clear();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = EDITOR_SIZE;
			io.DeltaTime = frame_time;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
			io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
			io.ConfigViewportsNoDecoration = false;
			io.ConfigDockingAlwaysTabBar = true;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
			platform.Platform_CreateWindow = [](ImGuiViewport* viewport) { windows[viewport] = ImRect(viewport->Pos, viewport->Pos + viewport->Size); };
			platform.Platform_DestroyWindow = [](ImGuiViewport* viewport) { windows.erase(viewport); };
			platform.Platform_ShowWindow = [](ImGuiViewport*) {};
			platform.Platform_SetWindowPos = [](ImGuiViewport* viewport, ImVec2 pos) { windows[viewport].TranslateX(pos.x - windows[viewport].Min.x); windows[viewport].TranslateY(pos.y - windows[viewport].Min.y); };
			platform.Platform_GetWindowPos = [](ImGuiViewport* viewport) { return windows[viewport].Min; };
			platform.Platform_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size) { windows[viewport].Max = windows[viewport].Min + size; };
			platform.Platform_GetWindowSize = [](ImGuiViewport* viewport) { return windows[viewport].GetSize(); };
			platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};

			ImGuiPlatformMonitor monitor;
			monitor.MainSize = monitor.WorkSize = MONITOR_SIZE;
			platform.Monitors.push_back(monitor);

			ImGuiViewport* const main = ImGui::GetMainViewport();
			main->PlatformHandle = context;
			windows[main] = ImRect(EDITOR_POS, EDITOR_POS + EDITOR_SIZE);
		};

		~Driven()
		{
			ImGui::DestroyPlatformWindows();
			ImGui::DestroyContext(context);
		};

		// One frame of the editor's order: the dockspace when docking, then the
		// panels, then the platform windows brought up to date.
		void Frame(bool dockspace)
		{
			ImGui::NewFrame();

			if (dockspace)
				dock = ImGui::DockSpaceOverViewport();

			ImGui::SetNextWindowPos(PANEL_POS, ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(PANEL_SIZE, ImGuiCond_FirstUseEver);
			ImGui::Begin(PANEL);
			ImGui::End();

			if (dockspace)
			{
				ImGui::Begin(OTHER);
				ImGui::End();
			};

			ImGui::EndFrame();
			ImGui::UpdatePlatformWindows();
		};

		// Docks both panels in the dockspace, as tabs of one node.
		void DockBoth()
		{
			Frame(true);
			ImGui::DockBuilderDockWindow(PANEL, dock);
			ImGui::DockBuilderDockWindow(OTHER, dock);
			ImGui::DockBuilderFinish(dock);
			Frame(true);
			Frame(true);
		};

		// Presses at from, moves to to in steps a frame apart and holds there.
		void Drag(ImVec2 from, ImVec2 to, bool dockspace)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(from.x, from.y);
			Frame(dockspace);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame(dockspace);

			for (int step = 1; step <= DRAG_STEPS; step++)
			{
				const float t = (float)step / DRAG_STEPS;
				io.AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
				Frame(dockspace);
			};
		};

		void Release(bool dockspace)
		{
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame(dockspace);
			Frame(dockspace);
		};

		static ImGuiWindow* Find(const char* name)
		{
			return ImGui::FindWindowByName(name);
		};

		// The middle of the window's tab.
		static ImVec2 Tab(ImGuiWindow* window)
		{
			REQUIRE(window->DockNode);
			const ImGuiTabBar* const tabs = window->DockNode->TabBar;
			REQUIRE(tabs);
			const ImGuiTabItem* const tab = ImGui::TabBarFindTabByID(const_cast<ImGuiTabBar*>(tabs), window->TabId);
			REQUIRE(tab);

			return ImVec2(tabs->BarRect.Min.x + tab->Offset + tab->Width * 0.5f, tabs->BarRect.GetCenter().y);
		};

		static bool InEditor(ImGuiWindow* window)
		{
			return window->Viewport == ImGui::GetMainViewport();
		};

		// Whether the window is all there is in a platform window of its own.
		static bool Alone(ImGuiWindow* window)
		{
			return !InEditor(window) && window->Viewport->Window == window->RootWindowDockTree && windows.count(window->Viewport) == 1;
		};

		static int Viewports()
		{
			return ImGui::GetPlatformIO().Viewports.Size;
		};

		inline static std::unordered_map<ImGuiViewport*, ImRect> windows;
		ImGuiContext* context = nullptr;
		ImGuiID dock = 0;
	};
};

TEST_SUITE("PanelTearOff")
{
	TEST_CASE("a panel dragged off the editor stays in it while held, and gets a window of its own where it is let go")
	{
		Driven driven;
		driven.Frame(false);
		driven.Frame(false);

		const ImVec2 grab = Driven::Tab(Driven::Find(PANEL));
		const ImVec2 offset = grab - Driven::Find(PANEL)->RootWindowDockTree->Pos;
		driven.Drag(grab, OFF_EDITOR, false);

		CHECK(Driven::InEditor(Driven::Find(PANEL)));
		CHECK(Driven::Viewports() == 1);

		driven.Release(false);

		ImGuiWindow* const window = Driven::Find(PANEL)->RootWindowDockTree;
		REQUIRE(Driven::Alone(window));
		CHECK(ImFabs(window->Pos.x - (OFF_EDITOR.x - offset.x)) <= PIXEL);
		CHECK(ImFabs(window->Pos.y - (OFF_EDITOR.y - offset.y)) <= PIXEL);
	};

	TEST_CASE("a panel let go over the editor's edge moves inside it rather than into a window")
	{
		constexpr float past_edge = 60.0f;

		Driven driven;
		driven.Frame(false);
		driven.Frame(false);

		const ImVec2 grab = Driven::Tab(Driven::Find(PANEL));
		const ImVec2 edge = ImVec2(EDITOR_POS.x + EDITOR_SIZE.x - past_edge, grab.y);
		driven.Drag(grab, edge, false);
		driven.Release(false);

		ImGuiWindow* const window = Driven::Find(PANEL)->RootWindowDockTree;
		CHECK(Driven::InEditor(window));
		CHECK(((ImGuiViewportP*)ImGui::GetMainViewport())->GetMainRect().Contains(window->Rect()));
		CHECK(Driven::Viewports() == 1);
	};

	TEST_CASE("a tab dragged off the editor leaves its dock on release, and its window closes once it is dragged back into the dock")
	{
		Driven driven;
		driven.DockBoth();

		driven.Drag(Driven::Tab(Driven::Find(PANEL)), OFF_EDITOR, true);

		CHECK(Driven::InEditor(Driven::Find(PANEL)));
		CHECK(Driven::Viewports() == 1);

		driven.Release(true);

		REQUIRE(Driven::Alone(Driven::Find(PANEL)));
		CHECK(Driven::InEditor(Driven::Find(OTHER)));
		CHECK(Driven::Viewports() == 2);

		const ImGuiDockNode* const central = ImGui::DockBuilderGetCentralNode(driven.dock);
		REQUIRE(central);
		driven.Drag(Driven::Tab(Driven::Find(PANEL)), central->Pos + central->Size * 0.5f, true);
		driven.Release(true);

		ImGuiWindow* const window = Driven::Find(PANEL);
		REQUIRE(window->DockNode);
		CHECK(ImGui::DockNodeGetRootNode(window->DockNode)->ID == driven.dock);
		CHECK(Driven::InEditor(window));
		CHECK(Driven::Viewports() == 1);
		CHECK(Driven::windows.size() == 1);
	};

	TEST_CASE("a tab dragged out of a torn-off window waits for release before it gets a window of its own")
	{
		Driven driven;
		driven.DockBoth();

		driven.Drag(Driven::Tab(Driven::Find(PANEL)), OFF_EDITOR, true);
		driven.Release(true);
		REQUIRE(Driven::Alone(Driven::Find(PANEL)));

		ImGui::DockBuilderDockWindow(OTHER, Driven::Find(PANEL)->DockId);
		driven.Frame(true);
		driven.Frame(true);
		REQUIRE(Driven::Find(OTHER)->Viewport == Driven::Find(PANEL)->Viewport);
		REQUIRE(Driven::Viewports() == 2);

		driven.Drag(Driven::Tab(Driven::Find(OTHER)), BELOW_EDITOR, true);

		CHECK(Driven::InEditor(Driven::Find(OTHER)));
		CHECK(Driven::Viewports() == 2);

		driven.Release(true);

		CHECK(Driven::Alone(Driven::Find(OTHER)));
		CHECK(Driven::Alone(Driven::Find(PANEL)));
		CHECK(Driven::Viewports() == 3);
	};
};

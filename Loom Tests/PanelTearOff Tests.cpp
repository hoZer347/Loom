#include "doctest.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include <unordered_map>

namespace
{
	constexpr const char* panel = "Tear Off Panel";
	constexpr const char* other = "Tear Off Other";

	constexpr ImVec2 EDITOR_POS = ImVec2(100.0f, 100.0f);
	constexpr ImVec2 EDITOR_SIZE = ImVec2(800.0f, 600.0f);
	constexpr ImVec2 MONITOR_SIZE = ImVec2(1920.0f, 1080.0f);
	constexpr ImVec2 PANEL_POS = ImVec2(300.0f, 300.0f);
	constexpr ImVec2 PANEL_SIZE = ImVec2(200.0f, 150.0f);
	constexpr ImVec2 OFF_EDITOR = ImVec2(1300.0f, 500.0f);
	constexpr int DRAG_STEPS = 8;

	// ImGui keeps window positions on whole pixels.
	constexpr float PIXEL = 1.0f;

	// An ImGui context with docking and viewports on, over a platform that
	// only remembers where each window is.
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

			ImGuiViewport* main = ImGui::GetMainViewport();
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
			ImGui::Begin(panel);
			ImGui::End();

			if (dockspace)
			{
				ImGui::Begin(other);
				ImGui::End();
			};

			ImGui::EndFrame();
			ImGui::UpdatePlatformWindows();
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

		static ImGuiWindow* Panel()
		{
			return ImGui::FindWindowByName(panel);
		};

		static ImVec2 TitleBar(ImGuiWindow* window)
		{
			return ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->TitleBarHeight * 0.5f);
		};

		static bool InEditor(ImGuiWindow* window)
		{
			return window->Viewport == ImGui::GetMainViewport();
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

		const ImVec2 grab = Driven::TitleBar(Driven::Panel());
		const ImVec2 offset = grab - Driven::Panel()->Pos;
		driven.Drag(grab, OFF_EDITOR, false);

		CHECK(Driven::InEditor(Driven::Panel()));
		CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);

		driven.Release(false);

		ImGuiWindow* window = Driven::Panel();
		REQUIRE_FALSE(Driven::InEditor(window));
		CHECK(window->ViewportOwned);
		CHECK(ImFabs(window->Pos.x - (OFF_EDITOR.x - offset.x)) <= PIXEL);
		CHECK(ImFabs(window->Pos.y - (OFF_EDITOR.y - offset.y)) <= PIXEL);
		CHECK(Driven::windows.count(window->Viewport) == 1);
	};

	TEST_CASE("a panel let go over the editor's edge moves inside it rather than into a window")
	{
		constexpr float past_edge = 60.0f;

		Driven driven;
		driven.Frame(false);

		const ImVec2 grab = Driven::TitleBar(Driven::Panel());
		const ImVec2 edge = ImVec2(EDITOR_POS.x + EDITOR_SIZE.x - past_edge, grab.y);
		driven.Drag(grab, edge, false);
		driven.Release(false);

		ImGuiWindow* window = Driven::Panel();
		CHECK(Driven::InEditor(window));
		CHECK(((ImGuiViewportP*)ImGui::GetMainViewport())->GetMainRect().Contains(window->Rect()));
		CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);
	};

	TEST_CASE("a tab dragged off the editor leaves its dock on release, and the window closes once its last tab docks back")
	{
		Driven driven;
		driven.Frame(true);

		ImGui::DockBuilderDockWindow(panel, driven.dock);
		ImGui::DockBuilderDockWindow(other, driven.dock);
		ImGui::DockBuilderFinish(driven.dock);
		driven.Frame(true);
		driven.Frame(true);

		ImGuiWindow* window = Driven::Panel();
		REQUIRE(window->DockNode);
		ImGuiTabBar* tabs = window->DockNode->TabBar;
		REQUIRE(tabs);
		const ImGuiTabItem* tab = ImGui::TabBarFindTabByID(tabs, window->TabId);
		REQUIRE(tab);

		const ImVec2 grab = ImVec2(tabs->BarRect.Min.x + tab->Offset + tab->Width * 0.5f, tabs->BarRect.GetCenter().y);
		driven.Drag(grab, OFF_EDITOR, true);

		CHECK(Driven::InEditor(Driven::Panel()));
		CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);

		driven.Release(true);

		REQUIRE_FALSE(Driven::InEditor(Driven::Panel()));
		CHECK(Driven::InEditor(ImGui::FindWindowByName(other)));
		CHECK(ImGui::GetPlatformIO().Viewports.Size == 2);

		ImGui::DockBuilderDockWindow(panel, driven.dock);
		driven.Frame(true);
		driven.Frame(true);

		CHECK(Driven::InEditor(Driven::Panel()));
		CHECK(ImGui::GetPlatformIO().Viewports.Size == 1);
		CHECK(Driven::windows.size() == 1);
	};
};

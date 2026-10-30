#include "doctest.h"

#include "TabHistory.h"
#include "UiScript.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>


TEST_SUITE("TabHistory")
{
	using Tab = Loom::TabHistory::Tab;

	constexpr Tab a = 1;
	constexpr Tab b = 2;
	constexpr Tab c = 3;
	constexpr Tab d = 4;

	const auto all_open = [](Tab) { return true; };

	Loom::TabHistory Focused(const std::vector<Tab>& tabs)
	{
		Loom::TabHistory history;

		for (const Tab tab : tabs)
			history.Focus(tab);

		return history;
	};

	#pragma region History

	TEST_CASE("back and forward walk the tabs focused, and stop at the ends")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		CHECK(history.Forward(all_open) == std::nullopt);
		CHECK(history.Back(all_open) == b);
		CHECK(history.Back(all_open) == a);
		CHECK(history.Back(all_open) == std::nullopt);
		CHECK(history.Forward(all_open) == b);
		CHECK(history.Forward(all_open) == c);
		CHECK(history.Forward(all_open) == std::nullopt);
	};

	TEST_CASE("an empty history goes nowhere")
	{
		Loom::TabHistory history;

		CHECK(history.Back(all_open) == std::nullopt);
		CHECK(history.Forward(all_open) == std::nullopt);
	};

	TEST_CASE("focusing the tab already current adds nothing")
	{
		Loom::TabHistory history = Focused({ a, b, b, b });

		CHECK(history.Back(all_open) == a);
		CHECK(history.Back(all_open) == std::nullopt);
	};

	TEST_CASE("arriving at a tab by navigating leaves what lies ahead")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		REQUIRE(history.Back(all_open) == b);
		history.Focus(b);

		CHECK(history.Forward(all_open) == c);
	};

	TEST_CASE("focusing a tab after going back drops what lay ahead")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		REQUIRE(history.Back(all_open) == b);
		history.Focus(d);

		CHECK(history.Forward(all_open) == std::nullopt);
		CHECK(history.Back(all_open) == b);
		CHECK(history.Back(all_open) == a);
	};

	TEST_CASE("a closed tab is skipped both ways")
	{
		Loom::TabHistory history = Focused({ a, b, c });
		const auto b_closed = [](Tab tab) { return tab != b; };

		CHECK(history.Back(b_closed) == a);
		CHECK(history.Forward(b_closed) == c);
	};

	TEST_CASE("a tab open again is back in the history")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		REQUIRE(history.Back([](Tab tab) { return tab != b; }) == a);

		CHECK(history.Forward(all_open) == b);
	};

	TEST_CASE("going back stays put when every earlier tab is closed")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		CHECK(history.Back([](Tab tab) { return tab == c; }) == std::nullopt);
		CHECK(history.Back(all_open) == b);
	};

	TEST_CASE("the tab left when the one between closes is not gone back to")
	{
		Loom::TabHistory history = Focused({ a, b, a });

		CHECK(history.Back([](Tab tab) { return tab != b; }) == std::nullopt);
	};

	TEST_CASE("the current tab closing leaves the one before it to go back to")
	{
		Loom::TabHistory history = Focused({ a, b, c });

		CHECK(history.Back([](Tab tab) { return tab != c; }) == b);
	};

	TEST_CASE("the oldest tabs fall off past the limit")
	{
		Loom::TabHistory history;

		for (int step = 0; step < Loom::TabHistory::max_steps + 1; step++)
			history.Focus(step);

		int steps_back = 0;
		std::optional<Tab> reached;

		while (const std::optional<Tab> tab = history.Back(all_open))
		{
			reached = tab;
			steps_back++;
		};

		CHECK(steps_back == Loom::TabHistory::max_steps - 1);
		CHECK(reached == Tab(1));
	};

	#pragma endregion

	#pragma region Dock

	struct Dock
	{
		explicit Dock(bool viewports = false)
			: viewports(viewports)
		{
			constexpr float frame_time = 1.0f / 60.0f;
			constexpr int settle_frames = 4;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.DisplaySize = ImVec2(display_width, display_height);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			raised.clear();

			if (viewports)
				UsePlatform();

			ImGui::NewFrame();

			const ImGuiID node = ImGui::DockSpaceOverViewport();

			for (const char* panel : panels)
				ImGui::DockBuilderDockWindow(panel, node);

			ImGui::DockBuilderFinish(node);
			EndFrame();

			Frames(settle_frames);
		};

		~Dock()
		{
			ImGui::DestroyPlatformWindows();
			ImGui::DestroyContext(context);
		};

		void UsePlatform()
		{
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
			io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;

			ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
			platform.Platform_CreateWindow = [](ImGuiViewport*) {};
			platform.Platform_DestroyWindow = [](ImGuiViewport*) {};
			platform.Platform_ShowWindow = [](ImGuiViewport*) {};
			platform.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2) {};
			platform.Platform_GetWindowPos = [](ImGuiViewport* viewport) { return viewport->Pos; };
			platform.Platform_SetWindowSize = [](ImGuiViewport*, ImVec2) {};
			platform.Platform_GetWindowSize = [](ImGuiViewport* viewport) { return viewport->Size; };
			platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
			platform.Platform_SetWindowFocus = [](ImGuiViewport* viewport) { raised.push_back(viewport); };

			ImGuiPlatformMonitor monitor;
			monitor.MainSize = monitor.WorkSize = ImVec2(monitor_width, display_height);
			platform.Monitors.push_back(monitor);

			ImGui::GetMainViewport()->PlatformHandle = context;
		};

		void Frames(int count)
		{
			for (int frame = 0; frame < count; frame++)
				Frame();
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::DockSpaceOverViewport();

			for (const char* panel : panels)
				if (!closed.contains(panel))
				{
					ImGui::Begin(panel);
					ImGui::End();
				};

			if (viewports)
			{
				ImGui::SetNextWindowPos(ImVec2(display_width * 2.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(display_width / 2.0f, display_height / 2.0f));
				ImGui::Begin(torn);
				ImGui::End();
			};

			if (popup)
			{
				ImGui::OpenPopup("Menu");
				ImGui::BeginPopup("Menu");
				ImGui::EndPopup();
			};

			history.Update(navigate);
			EndFrame();
		};

		void EndFrame()
		{
			ImGui::Render();

			if (viewports)
				ImGui::UpdatePlatformWindows();
		};

		void Run(const std::string& text)
		{
			constexpr int most_frames = 120;
			constexpr int settle_frames = 2;

			Loom::UiScript script{ std::filesystem::path() };

			script.LoadText(text);

			for (int frame = 0; frame < most_frames && !script.Finished(); frame++)
			{
				script.Step();
				Frame();
			};

			Frames(settle_frames);
		};

		void ClickTab(const char* panel)
		{
			const ImVec2 tab = ImGui::FindWindowByName(panel)->DC.DockTabItemRect.GetCenter();

			Run("click " + std::to_string((int)tab.x) + " " + std::to_string((int)tab.y));
		};

		void Back()
		{
			Run("backclick " + Middle());
		};

		void Forward()
		{
			Run("forwardclick " + Middle());
		};

		std::string Current() const
		{
			const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;

			if (focused == nullptr)
				return { };

			CHECK((focused->DockTabIsVisible || !focused->DockIsActive));

			return focused->Name;
		};

		static std::string Middle()
		{
			return std::to_string((int)(display_width / 2.0f)) + " " + std::to_string((int)(display_height / 2.0f));
		};

		static constexpr float display_width = 800.0f;
		static constexpr float display_height = 600.0f;
		static constexpr float monitor_width = display_width * 4.0f;
		static constexpr const char* panels[] = { "One", "Two", "Three" };
		static constexpr const char* torn = "Torn";

		static inline std::vector<ImGuiViewport*> raised{ };

		bool viewports;
		ImGuiContext* context = nullptr;
		Loom::TabHistory history;
		std::set<std::string> closed;
		bool navigate = true;
		bool popup = false;
	};

	TEST_CASE("the side buttons move focus along the tabs clicked, showing each")
	{
		Dock dock;

		dock.ClickTab("One");
		dock.ClickTab("Two");
		dock.ClickTab("Three");
		REQUIRE(dock.Current() == "Three");

		dock.Back();
		CHECK(dock.Current() == "Two");
		dock.Back();
		CHECK(dock.Current() == "One");
		dock.Forward();
		CHECK(dock.Current() == "Two");
		dock.Forward();
		CHECK(dock.Current() == "Three");
	};

	TEST_CASE("a tab clicked after going back ends the way forward")
	{
		Dock dock;

		dock.ClickTab("One");
		dock.ClickTab("Two");
		dock.ClickTab("Three");
		dock.Back();
		dock.Back();
		REQUIRE(dock.Current() == "One");

		dock.ClickTab("Three");
		dock.Forward();
		CHECK(dock.Current() == "Three");
		dock.Back();
		CHECK(dock.Current() == "One");
	};

	TEST_CASE("a closed panel is skipped")
	{
		Dock dock;

		dock.ClickTab("One");
		dock.ClickTab("Two");
		dock.ClickTab("Three");
		dock.closed.insert("Two");
		dock.Frames(2);

		dock.Back();
		CHECK(dock.Current() == "One");
	};

	TEST_CASE("the side buttons do nothing while something else has them, but focus is still recorded")
	{
		Dock dock;

		dock.ClickTab("One");
		dock.navigate = false;
		dock.ClickTab("Two");
		dock.Back();
		CHECK(dock.Current() == "Two");

		dock.navigate = true;
		dock.Back();
		CHECK(dock.Current() == "One");
	};

	TEST_CASE("the side buttons do nothing while a popup is open")
	{
		Dock dock;

		dock.ClickTab("One");
		dock.ClickTab("Two");
		dock.popup = true;
		dock.Back();
		dock.popup = false;
		dock.Frames(2);

		CHECK(dock.Current() == "Two");
	};

	TEST_CASE("the side buttons raise the OS window a panel is in, and only when it changes")
	{
		Dock dock{ true };

		dock.ClickTab("One");
		dock.ClickTab("Two");

		ImGuiWindow* window = ImGui::FindWindowByName(Dock::torn);
		REQUIRE(window->Viewport != ImGui::GetMainViewport());

		ImGui::FocusWindow(window);
		dock.Frame();
		REQUIRE(dock.Current() == Dock::torn);

		dock.Back();
		CHECK(dock.Current() == "Two");
		REQUIRE(Dock::raised.size() == 1);
		CHECK(Dock::raised.back() == ImGui::GetMainViewport());

		dock.Back();
		CHECK(dock.Current() == "One");
		CHECK(Dock::raised.size() == 1);

		dock.Forward();
		dock.Forward();
		CHECK(dock.Current() == Dock::torn);
		REQUIRE(Dock::raised.size() == 2);
		CHECK(Dock::raised.back() == window->Viewport);
	};

	#pragma endregion
};

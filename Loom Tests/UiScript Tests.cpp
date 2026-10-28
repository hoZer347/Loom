#include "doctest.h"

#include "Test Support.h"

#include "UiScript.h"

#include "Input.h"

#include "imgui.h"

#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#pragma region Helpers

namespace
{
	// An ImGui context of the test's own, with no window and no renderer,
	// and a script listening to it.
	struct ScriptedUi
	{
		explicit ScriptedUi(const std::string& text)
		{
			constexpr float width = 800.0f;
			constexpr float height = 600.0f;
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			// No folder: the tests write no files, and the web build has no
			// working directory to ask for.
			script = std::make_unique<Loom::UiScript>(std::filesystem::path());
			script->LoadText(text);
		};

		~ScriptedUi()
		{
			script.reset();
			ImGui::DestroyContext(context);
		};

		// Frames until the script ends, or there have been too many.
		void Run(const std::function<void()>& draw)
		{
			constexpr int most_frames = 600;

			for (int frame = 0; frame < most_frames && !script->Finished(); frame++)
			{
				script->Step();

				ImGui::NewFrame();
				draw();
				ImGui::Render();
			};
		};

		ImGuiContext* context = nullptr;
		std::unique_ptr<Loom::UiScript> script;
	};

	// A window placed where the tests can count on it.
	void Panel(const std::function<void()>& contents)
	{
		constexpr float corner = 10.0f;
		constexpr float side = 300.0f;

		ImGui::SetNextWindowPos(ImVec2(corner, corner));
		ImGui::SetNextWindowSize(ImVec2(side, side));

		if (ImGui::Begin("Panel"))
			contents();

		ImGui::End();
	};

};

#pragma endregion


TEST_SUITE("UiScript")
{
	#pragma region Mouse

	TEST_CASE("a click lands on the button with that label")
	{
		ScriptedUi ui("wait 2\nclick \"Panel\" \"Press me\"\n");
		int presses = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						ImGui::Button("Other");

						if (ImGui::Button("Press me"))
							presses++;
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
	};

	TEST_CASE("a label matches up to its ##, and an item can be aimed at by edge")
	{
		ScriptedUi ui("click \"\" \"Row\" right\n");
		int left = 0;
		int right = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						constexpr float half = 100.0f;

						if (ImGui::Button("Row##first", ImVec2(half, 0.0f)))
							left++;

						ImGui::SameLine(0.0f, 0.0f);

						if (ImGui::Button("##second", ImVec2(half, 0.0f)))
							right++;
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(left == 1);
		CHECK(right == 0);
	};

	TEST_CASE("an item that shows up later is waited for")
	{
		ScriptedUi ui("click \"Panel\" \"Late\"\n");
		constexpr int appears_at = 30;
		int frame = 0;
		int presses = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						if (++frame >= appears_at && ImGui::Button("Late"))
							presses++;
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
	};

	TEST_CASE("a double click is one, and a right click is not a left")
	{
		ScriptedUi ui("doubleclick \"Panel\" \"Target\"\nrightclick \"Panel\" \"Target\"\n");
		int doubles = 0;
		int rights = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						ImGui::Button("Target");

						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
							doubles++;

						if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
							rights++;
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(doubles == 1);
		CHECK(rights == 1);
	};

	// Enter first: inside the field, Ctrl+Z would undo the typing.
	TEST_CASE("typing goes into the field clicked, and a chord is pressed")
	{
		ScriptedUi ui("click \"Panel\" \"Name\"\ntype Little Cube\nkey enter\nkey ctrl+z\n");
		char name[64] = "";
		int undos = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						ImGui::InputText("Name", name, sizeof(name));

						if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteAlways))
							undos++;
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(std::string(name) == "Little Cube");
		CHECK(undos == 1);
	};

	TEST_CASE("a drag carries its payload from one item to the other")
	{
		ScriptedUi ui("drag \"Panel\" \"Source\" to \"Panel\" \"Target\"\n");
		constexpr int payload = 7;
		int dropped = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						ImGui::Button("Source");

						if (ImGui::BeginDragDropSource())
						{
							ImGui::SetDragDropPayload("test", &payload, sizeof(payload));
							ImGui::EndDragDropSource();
						};

						ImGui::Button("Target");

						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* delivered = ImGui::AcceptDragDropPayload("test"))
								dropped = *(const int*)delivered->Data;

							ImGui::EndDragDropTarget();
						};
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(dropped == payload);
	};

	TEST_CASE("hold mouse carries a drag through moves, so a target sees it before release")
	{
		ScriptedUi ui("move \"Panel\" \"Source\"\nhold mouse\nmove \"Panel\" \"Source\" right\nmove \"Panel\" \"Target\"\nwait 2\nrelease mouse\nwait 1\n");
		constexpr int payload = 7;
		int dropped = 0;
		bool hovered = false;
		bool hovered_before_drop = false;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						ImGui::Button("Source");

						if (ImGui::BeginDragDropSource())
						{
							ImGui::SetDragDropPayload("test", &payload, sizeof(payload));
							ImGui::EndDragDropSource();
						};

						ImGui::Button("Target");

						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* delivered = ImGui::AcceptDragDropPayload("test", ImGuiDragDropFlags_AcceptBeforeDelivery))
							{
								if (delivered->IsDelivery())
								{
									dropped = *(const int*)delivered->Data;
									hovered_before_drop = hovered;
								}
								else
									hovered = true;
							};

							ImGui::EndDragDropTarget();
						};
					});
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(hovered_before_drop);
		CHECK(dropped == payload);
	};

	TEST_CASE("a held mouse is let go of when the script ends")
	{
		ScriptedUi ui("hold mouse\nwait 1\n");
		ui.Run([]() { Panel([]() { }); });

		ImGui::NewFrame();
		ImGui::Render();
		Loom::Input::Tick();

		CHECK_FALSE(ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]);
		CHECK_FALSE(Loom::Input::GetMouseButton(ImGuiMouseButton_Left));
	};

	TEST_CASE("coordinates click where they say")
	{
		ScriptedUi ui("wait 2\nclick 60 60\n");
		int presses = 0;

		ui.Run(
			[&]()
			{
				constexpr float size = 100.0f;

				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(size + size, size + size));
				ImGui::Begin("Whole", nullptr, ImGuiWindowFlags_NoDecoration);
				ImGui::SetCursorScreenPos(ImVec2(size / 2.0f, size / 2.0f));

				if (ImGui::Button("Square", ImVec2(size, size)))
					presses++;

				ImGui::End();
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
	};

	TEST_CASE("the game hears the script's cursor, from the window's corner")
	{
		constexpr int x = 120;
		constexpr int y = 80;

		Loom::Input::ClearView();

		ScriptedUi ui("move 120 80\n");
		ImVec2 window;

		ui.Run([&]() { window = ImGui::GetMainViewport()->Size; });

		CHECK_FALSE(ui.script->Failed());
		CHECK(Loom::Input::mouse_x == x);
		CHECK(Loom::Input::mouse_y == y);
		CHECK(Loom::Input::screen_width == (int)window.x);
		CHECK(Loom::Input::screen_height == (int)window.y);
	};

	#pragma endregion

	#pragma region Failures

	TEST_CASE("a target that never shows up fails the script, saying which line")
	{
		const LoomTests::Captured errors(std::cerr);
		ScriptedUi ui("wait 1\nclick \"Panel\" \"Missing\"\nclick \"Panel\" \"Never reached\"\n");

		ui.Run([&]() { Panel([]() { ImGui::Button("Present"); }); });

		CHECK(ui.script->Finished());
		CHECK(ui.script->Failed());
		CHECK(errors.Says("line 2"));
	};

	TEST_CASE("an unknown command or key fails the script")
	{
		const LoomTests::Captured errors(std::cerr);

		for (const char* text : { "fly away\n", "key ctrl+nothing\n", "click \"Panel\"\n", "wait\n", "\"\" \"Cube\"\n", "hold\n", "release\n", "hold mouse right\n", "scroll\n", "scroll up\n", "resize 800\n", "resize wide 600\n", "resize 0 600\n" })
		{
			ScriptedUi ui(text);
			ui.Run([]() { });

			CHECK(ui.script->Failed());
		};

		CHECK(errors.Says("UI script line"));
	};

	#pragma endregion

	#pragma region Keys

	TEST_CASE("hold keeps ctrl down through a click until release")
	{
		ScriptedUi ui("hold ctrl\nclick \"Panel\" \"Press me\"\nrelease ctrl\nwait 1\n");
		bool ctrl_at_press = false;
		bool ctrl_after = true;
		int presses = 0;

		ui.Run(
			[&]()
			{
				Panel(
					[&]()
					{
						if (ImGui::Button("Press me"))
						{
							presses++;
							ctrl_at_press = ImGui::GetIO().KeyCtrl;
						};
					});

				ctrl_after = ImGui::GetIO().KeyCtrl;
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
		CHECK(ctrl_at_press);
		CHECK_FALSE(ctrl_after);
	};

	TEST_CASE("a held key is let go of when the script ends, quits or fails")
	{
		// GLFW's numbering, which the game hears keys by.
		constexpr int left_ctrl = 341;

		const LoomTests::Captured errors(std::cerr);

		for (const char* text : { "hold ctrl\nwait 1\n", "hold ctrl\nwait 1\nquit\n", "hold ctrl\nwait 1\nclick \"Panel\" \"Missing\"\n" })
		{
			ScriptedUi ui(text);
			ui.Run([]() { Panel([]() { }); });

			// The key-up lands on the frame after the script stops.
			ImGui::NewFrame();
			ImGui::Render();
			Loom::Input::Tick();

			CHECK_FALSE(ImGui::GetIO().KeyCtrl);
			CHECK_FALSE(Loom::Input::GetKey(left_ctrl));
		};

		CHECK(errors.Says("UI script line"));
	};

	TEST_CASE("the game hears a chord's keys, a modifier as its left one")
	{
		// GLFW's numbering, which the game hears keys by.
		constexpr int w_key = 'W';
		constexpr int left_shift = 340;

		ScriptedUi ui("hold shift\nkey w\nwait 1\nrelease shift\nwait 1\n");
		int presses = 0;
		bool shift_at_press = false;
		bool shift_after = true;

		ui.Run(
			[&]()
			{
				if (Loom::Input::GetKeyDown(w_key))
				{
					presses++;
					shift_at_press = Loom::Input::GetKey(left_shift);
				};

				shift_after = Loom::Input::GetKey(left_shift);

				// As the engine does, once a frame.
				Loom::Input::Tick();
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
		CHECK(shift_at_press);
		CHECK_FALSE(shift_after);
	};

	#pragma endregion

	#pragma region Scroll and Resize

	TEST_CASE("scroll turns the wheel where the mouse is, for ImGui and the game")
	{
		constexpr float notches = -2.0f;

		Loom::Input::ClearView();

		ScriptedUi ui("move 100 100\nscroll -2\nwait 2\n");
		float panels_heard = 0.0f;
		float game_heard = 0.0f;

		ui.Run(
			[&]()
			{
				panels_heard += ImGui::GetIO().MouseWheel;
				game_heard += Loom::Input::GetMouseWheel();

				Loom::Input::Tick();
			});

		CHECK_FALSE(ui.script->Failed());
		CHECK(panels_heard == notches);
		CHECK(game_heard == notches);
	};

	TEST_CASE("resize with no window to resize fails the script")
	{
		const LoomTests::Captured errors(std::cerr);
		ScriptedUi ui("resize 800 600\n");

		ui.Run([]() { });

		CHECK(ui.script->Failed());
		CHECK(errors.Says("no window"));
	};

	#pragma endregion

	#pragma region Parsing

	TEST_CASE("blank lines and comments are skipped, and quit ends early")
	{
		ScriptedUi ui("# a comment\n\nclick \"Panel\" \"Press me\"\nquit\nclick \"Panel\" \"Press me\"\n");
		int presses = 0;

		ui.Run([&]() { Panel([&]() { if (ImGui::Button("Press me")) presses++; }); });

		CHECK(ui.script->Finished());
		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
	};

	TEST_CASE("a line of only a vertical tab or form feed is blank")
	{
		ScriptedUi ui("\v\n\f\nquit\n");

		ui.Run([]() { });

		CHECK(ui.script->Finished());
		CHECK_FALSE(ui.script->Failed());
	};

	TEST_CASE("a wait or scroll of no finite number, or a wait of more frames than an int counts, fails the script")
	{
		const LoomTests::Captured errors(std::cerr);

		for (const char* text : { "wait nan\n", "wait inf\n", "wait 3e9\n", "scroll nan\n" })
		{
			ScriptedUi ui(text);
			ui.Run([]() { });

			CHECK(ui.script->Failed());
		};

		CHECK(errors.Says("UI script line"));
	};

	TEST_CASE("Run after quit or a failure runs only the new commands")
	{
		const LoomTests::Captured errors(std::cerr);

		for (const char* first : { "quit\nclick \"Panel\" \"Left over\"\n", "click \"Panel\" \"Missing\"\nclick \"Panel\" \"Left over\"\n" })
		{
			ScriptedUi ui(first);
			int left_over = 0;
			int fresh = 0;

			const auto draw = [&]()
			{
				Panel(
					[&]()
					{
						if (ImGui::Button("Left over"))
							left_over++;

						if (ImGui::Button("Fresh"))
							fresh++;
					});
			};

			ui.Run(draw);
			ui.script->Run("click \"Panel\" \"Fresh\"\n");
			ui.Run(draw);

			CHECK(ui.script->Finished());
			CHECK_FALSE(ui.script->Failed());
			CHECK(left_over == 0);
			CHECK(fresh == 1);
		};

		CHECK(errors.Says("UI script line"));
	};

	#pragma endregion

	#pragma region Windows

	TEST_CASE("items can be listed from one window, and only while listening")
	{
		ScriptedUi ui("wait 2\n");

		const auto draw = [&]()
		{
			Panel([]() { ImGui::Button("In panel"); });

			ImGui::Begin("Other");
			ImGui::Button("In other");
			ImGui::End();
		};

		ui.Run(draw);

		std::ostringstream other;
		ui.script->WriteItems(other, "Other");

		CHECK(other.str().find("In other") != std::string::npos);
		CHECK(other.str().find("In panel") == std::string::npos);

		ui.script->Listen(false);
		CHECK_FALSE(ui.script->Listening());

		ui.script->Run("wait 2\n");
		ui.Run(draw);

		std::ostringstream unheard;
		ui.script->WriteItems(unheard);

		CHECK(unheard.str().empty());
	};

	TEST_CASE("a window's region is where it was drawn, in framebuffer pixels, and none once it is closed")
	{
		constexpr float corner = 10.0f;
		constexpr float side = 300.0f;
		constexpr float scale = 2.0f;

		ScriptedUi ui("wait 2\n");
		ImGui::GetIO().DisplayFramebufferScale = ImVec2(scale, scale);

		ui.Run([]()
			{
				Panel(
					[]()
					{
						ImGui::BeginChild("Inner");
						ImGui::EndChild();
					});
			});

		const std::optional<Loom::Engine::Region> region = Loom::UiScript::WindowRegion("Pan");

		REQUIRE(region.has_value());
		CHECK(region->x == (int)(corner * scale));
		CHECK(region->y == (int)(corner * scale));
		CHECK(region->width == (int)(side * scale));
		CHECK(region->height == (int)(side * scale));

		// The panel's child is named after it, and is not what is meant.
		CHECK_FALSE(Loom::UiScript::WindowRegion("Inner").has_value());
		CHECK_FALSE(Loom::UiScript::WindowRegion("Missing").has_value());

		ui.script->Run("wait 2\n");
		ui.Run([]() { });

		CHECK_FALSE(Loom::UiScript::WindowRegion("Panel").has_value());
	};

	TEST_CASE("a docked window behind another tab has no region")
	{
		ScriptedUi ui("wait 1\n");
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// The dock builder needs settings loaded, which the first frame does.
		ui.Run([]() { });

		const ImGuiID node = ImGui::DockBuilderAddNode(0, ImGuiDockNodeFlags_None);
		ImGui::DockBuilderSetNodePos(node, ImVec2(0.0f, 0.0f));
		ImGui::DockBuilderSetNodeSize(node, ImGui::GetIO().DisplaySize);
		ImGui::DockBuilderDockWindow("Front", node);
		ImGui::DockBuilderDockWindow("Behind", node);
		ImGui::DockBuilderFinish(node);

		ui.script->Run("wait 3\n");
		ui.Run([]()
			{
				ImGui::Begin("Front");
				ImGui::End();
				ImGui::Begin("Behind");
				ImGui::End();
			});

		// The last docked window takes the tab.
		CHECK_FALSE(Loom::UiScript::WindowRegion("Front"));
		CHECK(Loom::UiScript::WindowRegion("Behind"));
	};

	TEST_CASE("a screenshot of a window not showing fails the script, saying which")
	{
		const LoomTests::Captured errors(std::cerr);
		ScriptedUi ui("wait 1\nscreenshot shot.bmp \"Missing\"\n");

		ui.Run([]() { Panel([]() { }); });

		CHECK(ui.script->Failed());
		CHECK(errors.Says("Missing"));
	};

	#pragma endregion
};

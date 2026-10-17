#include "doctest.h"

#include "UiScript.h"

#include "imgui.h"

#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

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

	// What the script says on stderr when it fails.
	struct CapturedErrors
	{
		CapturedErrors() : previous(std::cerr.rdbuf(text.rdbuf())) { };
		~CapturedErrors() { std::cerr.rdbuf(previous); };

		std::ostringstream text;
		std::streambuf* previous;
	};
};


TEST_SUITE("UiScript")
{
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

	TEST_CASE("a target that never shows up fails the script, saying which line")
	{
		CapturedErrors errors;
		ScriptedUi ui("wait 1\nclick \"Panel\" \"Missing\"\nclick \"Panel\" \"Never reached\"\n");

		ui.Run([&]() { Panel([]() { ImGui::Button("Present"); }); });

		CHECK(ui.script->Finished());
		CHECK(ui.script->Failed());
		CHECK(errors.text.str().find("line 2") != std::string::npos);
	};

	TEST_CASE("an unknown command or key fails the script")
	{
		CapturedErrors errors;

		for (const char* text : { "fly away\n", "key ctrl+nothing\n", "click \"Panel\"\n", "wait\n", "\"\" \"Cube\"\n" })
		{
			ScriptedUi ui(text);
			ui.Run([]() { });

			CHECK(ui.script->Failed());
		};
	};

	TEST_CASE("blank lines and comments are skipped, and quit ends early")
	{
		ScriptedUi ui("# a comment\n\nclick \"Panel\" \"Press me\"\nquit\nclick \"Panel\" \"Press me\"\n");
		int presses = 0;

		ui.Run([&]() { Panel([&]() { if (ImGui::Button("Press me")) presses++; }); });

		CHECK(ui.script->Finished());
		CHECK_FALSE(ui.script->Failed());
		CHECK(presses == 1);
	};

	TEST_CASE("Run after quit or a failure runs only the new commands")
	{
		CapturedErrors errors;

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
	};

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
};

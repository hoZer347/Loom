#include "doctest.h"

#include "EditorIcons.h"
#include "InputActions.h"
#include "KeyBindings.h"
#include "UiScript.h"

#include "imgui.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using Action = Loom::InputActions::Action;

	// A Key Bindings panel filling an ImGui context of the test's own, with no
	// window and no renderer, driven by a UiScript.
	struct Driven
	{
		Driven(const std::string& text, std::vector<Action> actions, std::vector<std::string> folders = { })
		{
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

			Loom::InputActions::Set(std::move(actions), std::move(folders));

			script = std::make_unique<Loom::UiScript>(std::filesystem::path());
			script->LoadText(text);
		};

		~Driven()
		{
			script.reset();
			ImGui::DestroyContext(context);
			Loom::InputActions::Set({ });
		};

		// Frames until the script ends; how many of them changed the actions.
		int Run(bool project_open = true)
		{
			constexpr int most_frames = 600;

			int changes = 0;
			bool open = true;

			for (int frame = 0; frame < most_frames && !script->Finished(); frame++)
			{
				script->Step();

				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(width, height));

				if (panel.Draw(&open, project_open))
					changes++;

				ImGui::Render();
			};

			return changes;
		};

		static constexpr float width = 800.0f;
		static constexpr float height = 600.0f;

		ImGuiContext* context = nullptr;
		std::unique_ptr<Loom::UiScript> script;
		Loom::KeyBindings panel;
	};

	const std::vector<Action> slowdown = { { "Slowdown", { Loom::Key::Space } } };

	// The panel is drawn before anything in it can be aimed at.
	constexpr const char* drawn = "wait 2\n";

	// A UiScript finds no combo by its label, so the first row's first key is
	// aimed at down from the Input column's header, inside its left edge.
	constexpr const char* first_key = "click \"Key Bindings\" \"Input\" left 20 20\n";

	// What a folder's row shows, for a UiScript to aim at.
	std::string FolderRow(const std::string& name)
	{
		return std::string(Loom::EditorIcons::Folder) + " " + name;
	};
};


TEST_SUITE("KeyBindings")
{
	TEST_CASE("Add Action adds one by a name no other has")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"Add Action\"\n" + drawn + "click \"Key Bindings\" \"Add Action\"\n", { { "Action", { } } });

		CHECK(driven.Run() == 2);
		CHECK_FALSE(driven.script->Failed());

		const std::vector<Action>& actions = Loom::InputActions::All();

		REQUIRE(actions.size() == 3);
		CHECK(actions[1].name == "Action 2");
		CHECK(actions[2].name == "Action 3");
	};

	TEST_CASE("a key picked from an action's list binds it")
	{
		Driven driven(std::string(drawn) + first_key + "click \"\" \"Comma\"\n", slowdown);

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All()[0].keys == std::vector<Loom::Key>{ Loom::Key::Comma });
	};

	TEST_CASE("None takes a key out of a combination, one not chosen yet too")
	{
		// The second slot's list sits right of the first's and its +.
		constexpr const char* second_key = "click \"Key Bindings\" \"Input\" left 250 20\n";

		Driven unchosen(std::string(drawn) + "click \"Key Bindings\" \"+\"\n" + drawn + second_key + drawn + "click \"\" \"None\"\n", slowdown);

		CHECK(unchosen.Run() == 2);
		CHECK_FALSE(unchosen.script->Failed());
		CHECK(Loom::InputActions::All()[0].keys == std::vector<Loom::Key>{ Loom::Key::Space });

		Driven chosen(std::string(drawn) + first_key + drawn + "click \"\" \"None\"\n", { { "Zoom In", { Loom::Key::LeftControl, Loom::Key::Equal } } });

		CHECK(chosen.Run() == 1);
		CHECK_FALSE(chosen.script->Failed());
		CHECK(Loom::InputActions::All()[0].keys == std::vector<Loom::Key>{ Loom::Key::Equal });
	};

	TEST_CASE("+ adds a key to be held with the others")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"+\"\n", slowdown);

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All()[0].keys == std::vector<Loom::Key>{ Loom::Key::Space, Loom::Key::None });
	};

	TEST_CASE("a name is taken once its box is let go of, and a blank one is not")
	{
		Driven renamed(std::string(drawn) + "click \"Key Bindings\" \"##Name\"\nkey ctrl+a\ntype Pause\nkey enter\n", slowdown);

		CHECK(renamed.Run() == 1);
		CHECK_FALSE(renamed.script->Failed());
		CHECK(Loom::InputActions::All()[0].name == "Pause");

		Driven blanked(std::string(drawn) + "click \"Key Bindings\" \"##Name\"\nkey ctrl+a\nkey delete\nkey enter\n", slowdown);

		CHECK(blanked.Run() == 0);
		CHECK_FALSE(blanked.script->Failed());
		CHECK(Loom::InputActions::All()[0].name == "Slowdown");
	};

	TEST_CASE("a name another action has is taken, as another way to do it")
	{
		// Listed by name, so Slowdown's box is the first.
		const std::vector<Action> two = { slowdown[0], { "Zoom", { Loom::Key::W } } };

		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"##Name\"\nkey ctrl+a\ntype Zoom\nkey enter\n", two);

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All()[0].name == "Zoom");
		CHECK(Loom::InputActions::All()[1].name == "Zoom");
	};

	TEST_CASE("a name a file could not keep is not taken")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"##Name\"\nkey ctrl+a\ntype Slowdown=1\nkey enter\n", slowdown);

		CHECK(driven.Run() == 0);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All() == slowdown);
	};

	TEST_CASE("Remove takes the action out")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"Remove\"\n", slowdown);

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All().empty());
	};

	TEST_CASE("with no project open there is nothing to change")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"Add Action\"\n", slowdown);

		CHECK(driven.Run(false) == 0);

		// Add Action was not there to click.
		CHECK(driven.script->Failed());
		CHECK(Loom::InputActions::All() == slowdown);
	};

	TEST_CASE("Add Folder makes one and has it named")
	{
		Driven driven(std::string(drawn) + "click \"Key Bindings\" \"Add Folder\"\n" + drawn + "key ctrl+a\ntype Camera\nkey enter\n", slowdown);

		CHECK(driven.Run() == 2);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::Folders() == std::vector<std::string>{ "Camera" });
		CHECK(Loom::InputActions::All() == slowdown);
	};

	TEST_CASE("an action dragged onto a folder is filed in it")
	{
		const std::string action = std::string(Loom::EditorIcons::Bindings) + "##action";

		Driven driven(std::string(drawn) + "drag \"Key Bindings\" \"" + action + "\" to \"Key Bindings\" \"" + FolderRow("Camera") + "\"\n", slowdown, { "Camera" });

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::All()[0].folder == "Camera");
	};

	TEST_CASE("Remove Folder leaves what it held a level up")
	{
		Driven driven(std::string(drawn) + "rightclick \"Key Bindings\" \"" + FolderRow("Camera") + "\"\nclick \"\" \"Remove Folder\"\n", { { "Pan Up", { Loom::Key::W }, "Camera" } }, { "Camera" });

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK(Loom::InputActions::Folders().empty());
		CHECK(Loom::InputActions::All()[0].folder.empty());
	};
};

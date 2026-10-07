#include "doctest.h"

#include "Test Support.h"

#include "EditCommands.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneHistory.h"
#include "SceneSerializer.h"

#include "imgui.h"

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

using LoomTests::Pump;
using Loom::EditCommand;

namespace
{
	// An ImGui context of the test's own, with no window and no renderer:
	// enough for NewFrame to read key events.
	struct ImGuiInput
	{
		ImGuiInput()
		{
			constexpr float width = 800.0f;
			constexpr float height = 600.0f;
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			// Every event lands in the frame it was sent before, rather than
			// being spread over frames to keep a press and release apart.
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
		};

		~ImGuiInput()
		{
			ImGui::DestroyContext(context);
		};

		// One frame: draw runs inside it, then the shortcut is read.
		EditCommand Frame(bool hierarchy_focused = false, const std::function<void()>& draw = nullptr)
		{
			ImGui::NewFrame();

			if (draw)
				draw();

			const EditCommand command = Loom::EditCommands::FromShortcut(hierarchy_focused);

			ImGui::EndFrame();

			return command;
		};

		// Holds the keys down for one frame and lets go before the next.
		EditCommand Press(
			std::initializer_list<ImGuiKey> keys,
			bool hierarchy_focused = false,
			const std::function<void()>& draw = nullptr)
		{
			Send(keys, true);
			const EditCommand command = Frame(hierarchy_focused, draw);
			Send(keys, false);
			Frame(hierarchy_focused, draw);

			return command;
		};

		static void Send(std::initializer_list<ImGuiKey> keys, bool down)
		{
			for (const ImGuiKey key : keys)
				ImGui::GetIO().AddKeyEvent(key, down);
		};

		ImGuiContext* context = nullptr;
	};

	// The scene a test edits, with one child to select.
	struct Selection : LoomTests::EngineFixture
	{
		Selection()
		{
			scene = new Loom::Scene("Clipboard");
			child = scene->AddChild("Child");
			Pump();

			grandchild = child->AddChild("Grandchild");
			Pump();
		};

		~Selection()
		{
			delete scene;
			Pump();
		};

		Loom::Scene* scene = nullptr;
		Loom::GameObject* child = nullptr;
		Loom::GameObject* grandchild = nullptr;
	};
};


TEST_SUITE("EditCommands")
{
	TEST_CASE("each shortcut asks for its edit")
	{
		ImGuiInput input;

		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Z }) == EditCommand::Undo);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Y }) == EditCommand::Redo);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiKey_Z }) == EditCommand::Redo);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_X }) == EditCommand::Cut);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_C }) == EditCommand::Copy);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_V }) == EditCommand::Paste);
		CHECK(input.Press({ ImGuiKey_Delete }, true) == EditCommand::Delete);
	};

	TEST_CASE("no keys, or keys that are not a shortcut, ask for nothing")
	{
		ImGuiInput input;

		CHECK(input.Frame() == EditCommand::None);
		CHECK(input.Press({ ImGuiKey_Z }) == EditCommand::None);
		CHECK(input.Press({ ImGuiKey_V }) == EditCommand::None);
		CHECK(input.Press({ ImGuiMod_Ctrl }) == EditCommand::None);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_A }) == EditCommand::None);

		// A chord is the exact set of modifiers, so an extra one is a
		// different shortcut.
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiMod_Alt, ImGuiKey_Z }) == EditCommand::None);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiKey_C }) == EditCommand::None);
	};

	TEST_CASE("Delete only counts while the Hierarchy has focus")
	{
		ImGuiInput input;

		CHECK(input.Press({ ImGuiKey_Delete }, false) == EditCommand::None);
		CHECK(input.Press({ ImGuiKey_Delete }, true) == EditCommand::Delete);

		// Ctrl+Delete is not Delete.
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Delete }, true) == EditCommand::None);
	};

	TEST_CASE("the Ctrl shortcuts work whether or not the Hierarchy has focus")
	{
		ImGuiInput input;

		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Z }, true) == EditCommand::Undo);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_V }, false) == EditCommand::Paste);
	};

	TEST_CASE("a shortcut held down asks once, not every frame")
	{
		constexpr int held_frames = 30;

		ImGuiInput input;

		ImGuiInput::Send({ ImGuiMod_Ctrl, ImGuiKey_Z }, true);

		CHECK(input.Frame() == EditCommand::Undo);

		for (int i = 0; i < held_frames; i++)
			CHECK(input.Frame() == EditCommand::None);

		ImGuiInput::Send({ ImGuiKey_Z }, false);
		input.Frame();

		// Ctrl still down: pressing Z again is a second undo.
		ImGuiInput::Send({ ImGuiKey_Z }, true);

		CHECK(input.Frame() == EditCommand::Undo);
	};

	TEST_CASE("a text box being typed into keeps the shortcuts for itself")
	{
		constexpr int frames_to_focus = 3;

		ImGuiInput input;

		char buffer[64]{ };

		const auto text_box =
			[&]()
			{
				ImGui::Begin("Typing");
				ImGui::SetKeyboardFocusHere();
				ImGui::InputText("##text", buffer, sizeof(buffer));
				ImGui::End();
			};

		for (int i = 0; i < frames_to_focus; i++)
			input.Frame(false, text_box);

		REQUIRE(ImGui::GetIO().WantTextInput);

		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Z }, true, text_box) == EditCommand::None);
		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_V }, true, text_box) == EditCommand::None);
		CHECK(input.Press({ ImGuiKey_Delete }, true, text_box) == EditCommand::None);

		// With the text box gone the keys are the editor's again.
		for (int i = 0; i < frames_to_focus; i++)
			input.Frame();

		REQUIRE_FALSE(ImGui::GetIO().WantTextInput);

		CHECK(input.Press({ ImGuiMod_Ctrl, ImGuiKey_Z }) == EditCommand::Undo);
	};

	TEST_CASE_FIXTURE(Selection, "only an object under a root can be taken")
	{
		CHECK_FALSE(Loom::EditCommands::CanTake(nullptr));
		CHECK_FALSE(Loom::EditCommands::CanTake(&scene->GetRoot()));
		CHECK(Loom::EditCommands::CanTake(child));
		CHECK(Loom::EditCommands::CanTake(grandchild));
	};

	TEST_CASE_FIXTURE(Selection, "a paste lands beside the selection")
	{
		CHECK(Loom::EditCommands::PasteParent(child, scene) == &scene->GetRoot());
		CHECK(Loom::EditCommands::PasteParent(grandchild, scene) == child);

		// The active scene does not pull a paste away from the selection.
		CHECK(Loom::EditCommands::PasteParent(grandchild, nullptr) == child);
	};

	TEST_CASE_FIXTURE(Selection, "a paste with a root selected lands under that root")
	{
		Loom::Scene other("Other");
		Pump();

		CHECK(Loom::EditCommands::PasteParent(&scene->GetRoot(), &other) == &scene->GetRoot());
	};

	TEST_CASE_FIXTURE(Selection, "a paste with nothing selected lands under the active scene's root")
	{
		CHECK(Loom::EditCommands::PasteParent(nullptr, scene) == &scene->GetRoot());
		CHECK(Loom::EditCommands::PasteParent(nullptr, nullptr) == nullptr);
	};

	TEST_CASE_FIXTURE(Selection, "copy then paste is one undo step, and undo takes only the copy back")
	{
		std::vector<Loom::Scene*> open{ scene };
		Loom::SceneHistory history;

		const Loom::SceneHistory::Rebuild rebuild =
			[&](Loom::Scene* old, const std::string& text)
			{
				scene = Loom::SceneHistory::Replace(old, text);
				open = { scene };
				return scene;
			};

		history.Record(open);

		const Loom::Guid original = child->GetGuid();
		const std::string copied = Loom::SceneSerializer::Serialize(*child);

		Loom::GameObject* pasted = Loom::SceneSerializer::Deserialize(
			copied,
			*Loom::EditCommands::PasteParent(child, scene));
		Pump();

		REQUIRE(pasted != nullptr);
		CHECK(scene->GetRoot().GetChildren().size() == 2);

		history.Record(open);

		REQUIRE(history.Undo(open, rebuild));

		REQUIRE(scene->GetRoot().GetChildren().size() == 1);
		CHECK(scene->GetRoot().GetChildren().front()->GetGuid() == original);

		REQUIRE(history.Redo(open, rebuild));
		CHECK(scene->GetRoot().GetChildren().size() == 2);
	};

	TEST_CASE_FIXTURE(Selection, "a cut brings the original back on undo, and pastes as a new copy")
	{
		std::vector<Loom::Scene*> open{ scene };
		Loom::SceneHistory history;

		const Loom::SceneHistory::Rebuild rebuild =
			[&](Loom::Scene* old, const std::string& text)
			{
				scene = Loom::SceneHistory::Replace(old, text);
				open = { scene };
				return scene;
			};

		history.Record(open);

		const Loom::Guid original = child->GetGuid();
		const Loom::Guid original_grandchild = grandchild->GetGuid();

		REQUIRE(Loom::EditCommands::CanTake(child));

		const std::string cut = Loom::SceneSerializer::Serialize(*child);
		child->Destroy();
		Pump();

		CHECK(scene->GetRoot().GetChildren().empty());

		history.Record(open);

		REQUIRE(history.Undo(open, rebuild));

		Loom::GameObject* restored = Loom::LoomObject::GetByGuid<Loom::GameObject>(original);

		REQUIRE(restored != nullptr);
		CHECK(restored->GetParent() == &scene->GetRoot());
		REQUIRE(restored->GetChildren().size() == 1);
		CHECK(restored->GetChildren().front()->GetGuid() == original_grandchild);

		// Pasting what was cut, with the original back, makes a copy rather
		// than a second object answering to the same guid.
		Loom::GameObject* pasted = Loom::SceneSerializer::Deserialize(
			cut,
			*Loom::EditCommands::PasteParent(nullptr, scene));
		Pump();

		REQUIRE(pasted != nullptr);
		CHECK(pasted->GetGuid() != original);
		CHECK(Loom::LoomObject::GetByGuid<Loom::GameObject>(original) == restored);
	};
};

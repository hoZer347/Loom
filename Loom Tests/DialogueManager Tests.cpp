#include "doctest.h"

// Text streams by the clock, which reads GLFW's time, and the web's clock cannot be
// slept on here.
#ifndef __EMSCRIPTEN__

#include "Test Support.h"

#include "GameObject.h"
#include "Scene.h"
#include "Ui.h"
#include "Utilities/Clock.h"
#include "Utilities/DialogueManager.h"

#include "GLFW/glfw3.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using LoomTests::Pump;

namespace
{
	// More frames than any line here takes to stream at no delay per character,
	// each long enough for the clock to see it pass.
	constexpr int MOST_FRAMES = 200;
	constexpr std::chrono::milliseconds FRAME_LENGTH{ 1 };

	// Each script here ends on [Pause], as the end of a dialogue clears the box.

	// A box as the Cutscene menu makes one: the manager above the text it fills.
	struct Box
	{
		Box()
		{
			glfwInit();

			object = scene.AddChild("Dialogue Box");
			Pump();

			GameObject* line = object->AddChild("Text");
			Pump();

			text = line->Attach<Loom::UiText>();
			text->text = std::string();
			manager = object->Attach<Loom::DialogueManager>();
			manager->startingTextSpeed = 0.0f;
			Pump();
		};

		~Box()
		{
			glfwTerminate();
		};

		// Frames of play until the box reads what it should, or MOST_FRAMES pass.
		bool Reads(const std::string& expected)
		{
			for (int frame = 0; frame < MOST_FRAMES && *text->text != expected; frame++)
			{
				std::this_thread::sleep_for(FRAME_LENGTH);
				Loom::Utilities::Tick();
				scene.Update();
				Loom::Utilities::Update();
			};

			return *text->text == expected;
		};

		using GameObject = Loom::GameObject;

		Loom::Scene scene{ "Dialogue" };
		GameObject* object = nullptr;
		Loom::UiText* text = nullptr;
		Loom::DialogueManager* manager = nullptr;
	};
};


TEST_SUITE("DialogueManager")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a manager streams its script into the UiText under it, passing RichText tags through whole")
	{
		Box box;

		box.manager->SetScript("Hi [Color, red]there[/Color].[Pause]");

		CHECK(box.Reads("Hi [Color, red]there[/Color]."));
		CHECK(box.manager->Text() == "Hi [Color, red]there[/Color].");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "with no script set, a manager streams the .dialogue file it names once it starts")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "DialogueManager Tests.dialogue";
		std::ofstream(path, std::ios::binary) << "From a file.[Pause]";

		Box box;

		box.manager->dialogue = path.string();

		CHECK(box.Reads("From a file."));

		std::filesystem::remove(path);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a manager names every command it has bound")
	{
		Loom::DialogueManager manager;
		std::vector<std::string> commands = manager.Commands();

		for (const char* name : { "Proceed", "Clear", "Pause", "TextSpeed" })
		{
			CAPTURE(name);
			CHECK(std::count(commands.begin(), commands.end(), name) == 1);
		};

		manager.Bind("Shout", []() { });
		commands = manager.Commands();

		CHECK(std::find(commands.begin(), commands.end(), "Shout") != commands.end());
		CHECK(std::is_sorted(commands.begin(), commands.end()));
	};
};

#endif

#include "doctest.h"

// Text streams by the clock, which reads GLFW's time, and the web's clock cannot be
// slept on here.
#ifndef __EMSCRIPTEN__

#include "Test Support.h"

#include "ComponentRegistry.h"
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
			manager = object->Attach<Loom::Basic::DialogueManager>();
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
		Loom::Basic::DialogueManager* manager = nullptr;
	};

	// A Say given a word rather than a number.
	constexpr int NOT_A_NUMBER = -1;

	struct Stage final : Loom::Component<Stage>
	{
		Loom::DialogueBinding<"Shake", float> shake = [this](float strength) { shaken = strength; };
		Loom::DialogueBinding<"Say", int> sayNumber = [this](int number) { said = number; };
		Loom::DialogueBinding<"Say", std::string> sayWord = [this](const std::string& word) { said = NOT_A_NUMBER; heard = word; };

		float shaken = 0.0f;
		int said = 0;
		std::string heard{ };
	};

	struct Announcer final : Loom::DialogueManager<Announcer>
	{
		Loom::DialogueBinding<"Shake", float> shake = [this](float strength) { shaken = strength; };

		float shaken = 0.0f;
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a space beside a RichText tag streams, and a line break between commands does not")
	{
		Box box;

		box.manager->SetScript("[Color, red]red[/Color] [Color, blue]blue[/Color]\n[Pause]");

		CHECK(box.Reads("[Color, red]red[/Color] [Color, blue]blue[/Color]"));
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
		Loom::Basic::DialogueManager manager;
		std::vector<std::string> commands = manager.Commands();

		for (const char* name : { "Proceed", "Clear", "Pause", "TextSpeed", "Color", "/Color", "Size", "/Size" })
		{
			CAPTURE(name);
			CHECK(std::count(commands.begin(), commands.end(), name) == 1);
		};

		manager.Bind("Shout", []() { });
		commands = manager.Commands();

		CHECK(std::find(commands.begin(), commands.end(), "Shout") != commands.end());
		CHECK(std::is_sorted(commands.begin(), commands.end()));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a DialogueBinding on the manager's GameObject runs with the script's arguments")
	{
		Box box;
		Stage* stage = box.object->Attach<Stage>();
		Pump();

		box.manager->SetScript("[Shake, 0.5]Shaken.[Pause]");

		CHECK(box.Reads("Shaken."));
		CHECK(stage->shaken == doctest::Approx(0.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a DialogueBinding on a parent of the manager's GameObject runs, and the manager names it")
	{
		Box box;
		Loom::GameObject* above = box.scene.AddChild("Stage");
		Pump();

		Stage* stage = above->Attach<Stage>();
		box.object->SetParent(above);
		Pump();

		const std::vector<std::string> commands = box.manager->Commands();

		CHECK(std::count(commands.begin(), commands.end(), "Shake") == 1);
		CHECK(std::count(commands.begin(), commands.end(), "Say") == 1);

		box.manager->SetScript("[shake, 2]Shaken.[Pause]");

		CHECK(box.Reads("Shaken."));
		CHECK(stage->shaken == doctest::Approx(2.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a DialogueBinding's forms are tried in the order they were declared")
	{
		{
			Box box;
			Stage* stage = box.object->Attach<Stage>();
			Pump();

			box.manager->SetScript("[Say, 7]Number.[Pause]");

			CHECK(box.Reads("Number."));
			CHECK(stage->said == 7);
		};

		Box box;
		Stage* stage = box.object->Attach<Stage>();
		Pump();

		box.manager->SetScript("[Say, hello]Word.[Pause]");

		CHECK(box.Reads("Word."));
		CHECK(stage->said == NOT_A_NUMBER);
		CHECK(stage->heard == "hello");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the manager's own commands and the RichText tags are DialogueBindings of its own")
	{
		Loom::Basic::DialogueManager manager;
		std::vector<std::string> own;

		for (const Loom::DialogueBindings::Form& form : Loom::DialogueBindings::Of(&manager))
			own.push_back(form.name);

		for (const char* name : { "Proceed", "Clear", "Pause", "TextSpeed", "Color", "/Color", "Size", "/Size" })
		{
			CAPTURE(name);
			CHECK(std::find(own.begin(), own.end(), name) != own.end());
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a manager's own DialogueBinding runs before one of the same name beside it")
	{
		Box box;
		Stage* stage = box.object->Attach<Stage>();
		Announcer* announcer = box.object->Attach<Announcer>();
		announcer->startingTextSpeed = 0.0f;
		box.object->DetachComponent(box.manager);
		Pump();

		announcer->SetScript("[Shake, 4]Shaken.[Pause]");

		CHECK(box.Reads("Shaken."));
		CHECK(announcer->shaken == doctest::Approx(4.0f));
		CHECK(stage->shaken == doctest::Approx(0.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a project's DialogueManager is filed under Custom/Dialogues, and the Basic one under Logic as DialogueManager")
	{
		CHECK(Loom::ComponentRegistry::CategoryOf("Announcer") == "Custom/Dialogues");
		CHECK(Loom::ComponentRegistry::CategoryOf("DialogueManager") == "Logic");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a DialogueBinding is called like the function it holds, and declares its name until it is gone")
	{
		Box box;
		Stage* stage = box.object->Attach<Stage>();
		Pump();

		stage->shake(3.0f);

		CHECK(stage->shaken == doctest::Approx(3.0f));
		CHECK(static_cast<bool>(stage->shake));

		const std::vector<std::string> names = Loom::DialogueBindings::Names();

		CHECK(std::find(names.begin(), names.end(), "Shake") != names.end());
		CHECK(Loom::DialogueBindings::Of(stage).size() == 3);

		const Loom::LoomObject* gone = stage;
		box.object->DetachComponent(stage);
		Pump();

		CHECK(Loom::DialogueBindings::Of(gone).empty());
	};
};

#endif

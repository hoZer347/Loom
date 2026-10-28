#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "BuildSettings.h"
#include "UiScript.h"

#include "imgui.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
	// A Build Settings tab filling an ImGui context of the test's own, with
	// no window and no renderer, driven by a UiScript.
	struct Driven
	{
		explicit Driven(const std::string& text)
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

			script = std::make_unique<Loom::UiScript>(std::filesystem::path());
			script->LoadText(text);

			settings.Save(file);
		};

		// A script of its own to carry on with, since one that has ended stays ended.
		void Next(const std::string& text)
		{
			script = std::make_unique<Loom::UiScript>(std::filesystem::path());
			script->LoadText(text);
		};

		~Driven()
		{
			script.reset();
			ImGui::DestroyContext(context);
			std::filesystem::remove(file);
		};

		// Frames until the script ends; how many of them asked to save, each
		// writing to file as the editor does.
		int Run()
		{
			constexpr int most_frames = 600;

			int saves = 0;
			bool open = true;

			for (int frame = 0; frame < most_frames && !script->Finished(); frame++)
			{
				script->Step();

				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(width, height));

				if (settings.Draw(&open, "C:/Games/MyGame", "Space", []() { return std::vector<std::string>{ }; }, "", false) == Loom::BuildSettings::Action::Save)
				{
					saves++;
					REQUIRE(settings.Save(file));
				};

				ImGui::Render();
			};

			return saves;
		};

		static constexpr float width = 800.0f;
		static constexpr float height = 600.0f;

		ImGuiContext* context = nullptr;
		std::unique_ptr<Loom::UiScript> script;
		Loom::BuildSettings settings;

		const std::string file = (std::filesystem::temp_directory_path() / "Loom BuildSettings Driven.loombuild").string();

		std::string OnDisk() const
		{
			std::ifstream in(file, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
	};

	// The tab is drawn before anything in it can be aimed at.
	constexpr const char* drawn = "wait 2\n";

	constexpr const char* typed_folder = "click \"Build Settings\" \"##Folder\"\ntype D:/Out\nkey enter\n";
};


TEST_SUITE("BuildSettings")
{
	TEST_CASE("the settings come back from their file as they were saved")
	{
		const std::string path = (std::filesystem::temp_directory_path() / "Loom BuildSettings Tests.loombuild").string();

		Loom::BuildSettings saved;
		saved.configuration = "Final";
		saved.scene = "Scenes/Start Here.loomscene";
		saved.folder = "C:/Games/My Game Build";

		REQUIRE(saved.Save(path));

		Loom::BuildSettings loaded;
		loaded.Load(path);

		CHECK(loaded.configuration == saved.configuration);
		CHECK(loaded.scene == saved.scene);
		CHECK(loaded.folder == saved.folder);

		std::filesystem::remove(path);
	};

	TEST_CASE("a file with nothing in it, or a configuration there is none of, keeps the defaults")
	{
		const std::string path = (std::filesystem::temp_directory_path() / "Loom BuildSettings Defaults.loombuild").string();

		std::ofstream(path, std::ios::binary) << "configuration=Profile\r\n";

		Loom::BuildSettings loaded;
		loaded.Load(path);

		CHECK(loaded.configuration == Loom::BuildSettings{ }.configuration);
		CHECK(loaded.scene.empty());
		CHECK(loaded.folder.empty());

		loaded.Load((std::filesystem::temp_directory_path() / "Loom BuildSettings Missing.loombuild").string());

		CHECK(loaded.configuration == Loom::BuildSettings{ }.configuration);

		std::filesystem::remove(path);
	};

	TEST_CASE("with no folder chosen, the game goes beside the project folder rather than in it")
	{
		const Loom::BuildSettings settings;
		const std::filesystem::path output = settings.Output("C:/Games/MyGame", "Space");

		CHECK(output == std::filesystem::path("C:/Games/Space Build"));
		CHECK(output.parent_path() == std::filesystem::path("C:/Games"));
	};

	TEST_CASE("a folder chosen is taken from the project folder unless it is absolute")
	{
		Loom::BuildSettings settings;

		settings.folder = "../Shipped";
		CHECK(settings.Output("C:/Games/MyGame", "Space") == std::filesystem::path("C:/Games/Shipped"));

		settings.folder = "D:/Out/Space";
		CHECK(settings.Output("C:/Games/MyGame", "Space") == std::filesystem::path("D:/Out/Space"));
	};

	TEST_CASE("the script is told the project, the scene from it, the configuration and the folder, quoted whole")
	{
		Loom::BuildSettings settings;
		settings.configuration = "Debug";
		settings.scene = "Scenes/Start.loomscene";

		CHECK(
			settings.Arguments("C:\\My Game", "Space") ==
			"-Project \"C:\\My Game\" -Scene \"C:\\My Game\\Scenes\\Start.loomscene\" -Configuration Debug -Output \"C:\\Space Build\"");
	};

	TEST_CASE("the game is the program named after it in the folder it is built in")
	{
		Loom::BuildSettings settings;

		CHECK(settings.Program("C:/Games/MyGame", "Space") == std::filesystem::path("C:/Games/Space Build/Space.exe"));

		settings.folder = "D:/Out";
		CHECK(settings.Program("C:/Games/MyGame", "Space") == std::filesystem::path("D:/Out/Space.exe"));
	};

	TEST_CASE("settings just loaded or saved are not unsaved, and a change to them is")
	{
		const std::string path = (std::filesystem::temp_directory_path() / "Loom BuildSettings Unsaved.loombuild").string();

		Loom::BuildSettings settings;
		CHECK_FALSE(settings.Unsaved());

		settings.configuration = "Debug";
		CHECK(settings.Unsaved());

		REQUIRE(settings.Save(path));
		CHECK_FALSE(settings.Unsaved());

		Loom::BuildSettings loaded;
		loaded.folder = "D:/Out";
		loaded.Load(path);
		CHECK_FALSE(loaded.Unsaved());

		std::filesystem::remove(path);
	};

	TEST_CASE("the scene started on until one is chosen is not a change")
	{
		Loom::BuildSettings settings;
		settings.DefaultScene("Main.loomscene");

		CHECK(settings.scene == "Main.loomscene");
		CHECK_FALSE(settings.Unsaved());
	};

	TEST_CASE("a folder typed is held until Ctrl+S, and Ctrl+Z and Ctrl+Y step it")
	{
		Driven driven(std::string(drawn) + typed_folder + drawn);
		const std::string loaded = driven.OnDisk();

		CHECK(driven.Run() == 0);
		CHECK(driven.settings.folder == "D:/Out");
		CHECK(driven.settings.Unsaved());
		CHECK(driven.OnDisk() == loaded);

		driven.Next("key ctrl+z\n" + std::string(drawn));
		driven.Run();

		CHECK(driven.settings.folder.empty());
		CHECK_FALSE(driven.settings.Unsaved());

		driven.Next("key ctrl+y\n" + std::string(drawn));
		driven.Run();

		CHECK(driven.settings.folder == "D:/Out");

		driven.Next("key ctrl+s\n" + std::string(drawn));

		CHECK(driven.Run() == 1);
		CHECK_FALSE(driven.script->Failed());
		CHECK_FALSE(driven.settings.Unsaved());
		CHECK(driven.OnDisk().find("folder=D:/Out") != std::string::npos);
	};

	TEST_CASE("only the player's own configurations are ones to build")
	{
		for (const std::string_view configuration : Loom::BuildSettings::configurations)
			CHECK(Loom::BuildSettings::IsConfiguration(configuration));

		CHECK_FALSE(Loom::BuildSettings::IsConfiguration("Profile"));
		CHECK_FALSE(Loom::BuildSettings::IsConfiguration(""));
	};
};

#endif

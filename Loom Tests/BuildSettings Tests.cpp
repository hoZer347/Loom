#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "BuildSettings.h"

#include <filesystem>
#include <fstream>


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
			settings.Arguments("C:\\My Game", "Space", false) ==
			"-Project \"C:\\My Game\" -Scene \"C:\\My Game\\Scenes\\Start.loomscene\" -Configuration Debug -Output \"C:\\Space Build\"");

		CHECK(settings.Arguments("C:\\My Game", "Space", true).ends_with(" -Run"));
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

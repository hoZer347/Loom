#include "doctest.h"

#include "NativePlayer.h"

#include <filesystem>


TEST_SUITE("NativePlayer")
{
	TEST_CASE("each configuration builds into a folder of its own under the project's build folder")
	{
		CHECK(Loom::NativePlayer::Folder("C:/Games/MyGame", "Release") == std::filesystem::path("C:/Games/MyGame/Build/Release"));
		CHECK(Loom::NativePlayer::Folder("C:/Games/MyGame", "Final") != Loom::NativePlayer::Folder("C:/Games/MyGame", "Release"));
	};

	TEST_CASE("the script is told the project, the scene and the configuration, with folders quoted whole")
	{
		CHECK(
			Loom::NativePlayer::Arguments("C:\\My Game\\", "C:\\My Game\\Build\\Debug\\Scene.loomscene", "Debug") ==
			"-Project \"C:\\My Game\" -Scene \"C:\\My Game\\Build\\Debug\\Scene.loomscene\" -Configuration Debug");
	};

	TEST_CASE("only the player's own configurations are ones to build")
	{
		for (const std::string_view configuration : Loom::NativePlayer::configurations)
			CHECK(Loom::NativePlayer::IsConfiguration(configuration));

		CHECK_FALSE(Loom::NativePlayer::IsConfiguration("Profile"));
		CHECK_FALSE(Loom::NativePlayer::IsConfiguration(""));
	};
};

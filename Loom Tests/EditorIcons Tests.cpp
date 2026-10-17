#include "doctest.h"

#include "EditorIcons.h"
#include "ProjectAssets.h"


TEST_SUITE("EditorIcons")
{
	TEST_CASE("each kind of file has its own icon, whatever the extension's case")
	{
		using namespace Loom;

		CHECK(EditorIcons::Of("Scenes/Main.loomscene") == EditorIcons::Scene);
		CHECK(EditorIcons::Of("Scenes/Main.LOOMSCENE") == EditorIcons::Scene);
		CHECK(EditorIcons::Of("Assets/Lit.shader") == EditorIcons::Shader);
		CHECK(EditorIcons::Of("Assets/Lit.Shader") == EditorIcons::Shader);
		CHECK(EditorIcons::Of("Game.loomproject") == EditorIcons::Project);
		CHECK(EditorIcons::Of("Game.LoomProject") == EditorIcons::Project);
		CHECK(EditorIcons::Of("Game.loomscripts") == EditorIcons::Script);
		CHECK(EditorIcons::Of("Game.LoomScripts") == EditorIcons::Script);
		CHECK(EditorIcons::Of("Assets/Tree.png") == EditorIcons::Texture);
		CHECK(EditorIcons::Of("Assets/Tree.JPEG") == EditorIcons::Texture);
		CHECK(EditorIcons::Of("Assets/Bell.wav") == EditorIcons::Sound);
		CHECK(EditorIcons::Of("Assets/Theme.MP3") == EditorIcons::Sound);
		CHECK(EditorIcons::Of("README.txt") == EditorIcons::File);
		CHECK(EditorIcons::Of("Makefile") == EditorIcons::File);
	};

	TEST_CASE("a panel's ID is what follows ### in its title")
	{
		using namespace Loom;

		CHECK(EditorPanels::IdOf(EditorPanels::Hierarchy) == "Hierarchy");
		CHECK(EditorPanels::IdOf(EditorPanels::SceneData) == "Scene Data");
		CHECK(EditorPanels::IdOf(EditorPanels::SceneId) == "Scene");
	};

	TEST_CASE("a texture is whatever stb_image decodes, whatever the extension's case")
	{
		using Loom::ProjectAssets;

		for (const char* name : { "a.png", "a.jpg", "a.jpeg", "a.bmp", "a.tga", "a.PNG", "a.Tga" })
			CHECK(ProjectAssets::IsTextureFile(name));

		for (const char* name : { "a.shader", "a.loomscene", "a.hpp", "a.gif", "png" })
			CHECK_FALSE(ProjectAssets::IsTextureFile(name));

		CHECK(ProjectAssets::IsGameFile("a.png"));
	};
};

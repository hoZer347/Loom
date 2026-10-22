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
		CHECK(EditorIcons::Of("Game.loom2d") == EditorIcons::TwoD);
		CHECK(EditorIcons::Of("Game.loom3d") == EditorIcons::ThreeD);
		CHECK(EditorIcons::Of("Game.loomsounds") == EditorIcons::Sound);
		CHECK(EditorIcons::Of("Game.LoomFonts") == EditorIcons::Font);
		CHECK(EditorIcons::Of("Game.loominput") == EditorIcons::Bindings);
		CHECK(EditorIcons::Of("Game.LoomInput") == EditorIcons::Bindings);
		CHECK(EditorIcons::Of("Assets/Tree.png") == EditorIcons::Texture);
		CHECK(EditorIcons::Of("Assets/Tree.loomsprite") == EditorIcons::Sprite);
		CHECK(EditorIcons::Of("Assets/Tree.LoomSprite") == EditorIcons::Sprite);
		CHECK(EditorIcons::Of("Assets/Forest.loompalette") == EditorIcons::Palette);
		CHECK(EditorIcons::Of("Assets/Forest.LoomPalette") == EditorIcons::Palette);
		CHECK(EditorIcons::Of("Assets/Tree.JPEG") == EditorIcons::Texture);
		CHECK(EditorIcons::Of("Assets/Bell.wav") == EditorIcons::Sound);
		CHECK(EditorIcons::Of("Assets/Theme.MP3") == EditorIcons::Sound);
		CHECK(EditorIcons::Of("Assets/Fonts/Roboto.ttf") == EditorIcons::Font);
		CHECK(EditorIcons::Of("Assets/Fonts/Inter.OTF") == EditorIcons::Font);
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

	TEST_CASE("the 2D panel's files are textures, sprites and palettes")
	{
		using Loom::ProjectAssets;

		for (const char* name : { "a.png", "a.TGA", "a.loomsprite", "a.loompalette", "Art/a.LoomPalette" })
			CHECK(ProjectAssets::IsTwoDFile(name));

		for (const char* name : { "a.wav", "a.loomscene", "a.shader", "a.loom2d", "loomsprite" })
			CHECK_FALSE(ProjectAssets::IsTwoDFile(name));
	};

	TEST_CASE("a font is a TrueType or OpenType file or collection, whatever the extension's case")
	{
		using Loom::ProjectAssets;

		for (const char* name : { "a.ttf", "a.otf", "a.ttc", "a.TTF", "a.Otf" })
			CHECK(ProjectAssets::IsFontFile(name));

		for (const char* name : { "a.woff2", "a.png", "a.fnt", "ttf" })
			CHECK_FALSE(ProjectAssets::IsFontFile(name));

		CHECK(ProjectAssets::IsGameFile("Assets/Fonts/Roboto-Medium.ttf"));
	};
};

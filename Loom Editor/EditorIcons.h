#pragma once

#include <string>
#include <string_view>


namespace Loom
{
	/*
	* Every kind of asset, panel and document tab has an emoji, so it can be
	* told apart at a glance. EditorTheme merges Segoe UI Emoji into the UI
	* font for them. A texture shows itself where there is room for a picture,
	* and this only where there is not.
	*/
	namespace EditorIcons
	{
		constexpr const char* Folder = "📁";
		constexpr const char* Project = "🧵";
		constexpr const char* Scene = "🎬";
		constexpr const char* Prefab = "📦";
		constexpr const char* Shader = "✨";
		constexpr const char* PixelShader = "🖥";
		constexpr const char* Texture = "🖼";
		constexpr const char* TwoD = "🎨";
		constexpr const char* ThreeD = "🗿";
		constexpr const char* Sprite = "👾";
		constexpr const char* Palette = "🌈";
		constexpr const char* Sound = "🔊";
		constexpr const char* Font = "🔤";
		constexpr const char* Model = "🧊";
		constexpr const char* Script = "🧩";
		constexpr const char* State = "🚦";
		constexpr const char* Dialogue = "🗨";
		constexpr const char* Test = "🧪";
		constexpr const char* Bindings = "⌨";
		constexpr const char* Build = "🔨";
		constexpr const char* File = "📄";

		// What the Project panel's tabs show beside a file, models
		// aside, which only ModelImporter can tell.
		const char* Of(const std::string& path);
	};

	// The panels' titles. What follows ### is the ID ImGui saves the layout
	// under, and what DockBuilderDockWindow and FindWindowByName can be given.
	namespace EditorPanels
	{
		constexpr const char* Hierarchy = "🌳 Hierarchy###Hierarchy";
		constexpr const char* Project = "📁 Project###Project";
		constexpr const char* Inspector = "🔍 Inspector###Inspector";
		constexpr const char* Game = "🎮 Game###Game";
		constexpr const char* Debug = "🐞 Debug###Debug";
		constexpr const char* PrefabId = "###Prefab";
		constexpr const char* SceneData = "📊 Scene Data###Scene Data";
		constexpr const char* Console = "💬 Console###Console";
		constexpr const char* Shaders = "✨ Shaders###Shaders";
		constexpr const char* Dialogues = "🗨 Dialogues###Dialogues";
		constexpr const char* Sprites = "🖌 Sprites###Sprites";
		constexpr const char* Model = "🧊 Model###Model";
		constexpr const char* Palette = "🌈 Palette###Palette";
		constexpr const char* Stats = "📈 Stats###Stats";
		constexpr const char* Settings = "⚙ Settings###Settings";
		constexpr const char* BuildSettings = "🔨 Build Settings###Build Settings";
		constexpr const char* Tests = "🧪 Tests###Tests";
		constexpr const char* KeyBindings = "⌨ Key Bindings###Key Bindings";

		// The part of a title after ###.
		constexpr std::string_view IdOf(std::string_view title)
		{
			constexpr std::string_view marker = "###";

			return title.substr(title.find(marker) + marker.size());
		};
	};

	// The tabs of the Project panel, each the project's files of one kind.
	namespace ProjectTabs
	{
		constexpr const char* Scenes = "🎬 Scenes";
		constexpr const char* Scripts = "🧩 Scripts";
		constexpr const char* TwoD = "🎨 2D";
		constexpr const char* ThreeD = "🗿 3D";
		constexpr const char* Sounds = "🔊 Sounds";
		constexpr const char* Fonts = "🔤 Fonts";
	};
};

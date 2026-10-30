#include "doctest.h"

// Not in the web test build, which has no editor.
#ifndef __EMSCRIPTEN__

#include "EditorIcons.h"
#include "EditorTheme.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstring>
#include <filesystem>


TEST_SUITE("EditorTheme")
{
	TEST_CASE("the emoji fonts draw every icon in the panel titles and tabs")
	{
		using namespace Loom;

		constexpr const char* labels[] =
		{
			EditorIcons::Folder, EditorIcons::Project, EditorIcons::Scene, EditorIcons::Prefab,
			EditorIcons::Shader, EditorIcons::PixelShader, EditorIcons::ComputeShader, EditorIcons::PreVertex,
			EditorIcons::Texture, EditorIcons::TwoD, EditorIcons::ThreeD, EditorIcons::Sprite,
			EditorIcons::Palette, EditorIcons::Sound, EditorIcons::Font, EditorIcons::Model,
			EditorIcons::Script, EditorIcons::State, EditorIcons::Dialogue, EditorIcons::Test,
			EditorIcons::Bindings, EditorIcons::Build, EditorIcons::File, EditorIcons::New,
			EditorIcons::Play, EditorIcons::Pause, EditorIcons::Stop, EditorIcons::Step,
			EditorPanels::Hierarchy, EditorPanels::Project, EditorPanels::Inspector, EditorPanels::Game,
			EditorPanels::Debug, EditorPanels::SceneData, EditorPanels::Console, EditorPanels::Shaders,
			EditorPanels::Dialogues, EditorPanels::Sprites, EditorPanels::Model, EditorPanels::Palette,
			EditorPanels::Stats, EditorPanels::Settings, EditorPanels::BuildSettings, EditorPanels::Tests,
			EditorPanels::KeyBindings,
			ProjectTabs::Scenes, ProjectTabs::Scripts, ProjectTabs::TwoD, ProjectTabs::ThreeD,
			ProjectTabs::Sounds, ProjectTabs::Fonts,
		};

		// Past the punctuation the UI font has of its own.
		constexpr unsigned int first_symbol = 0x2000;

		ImGuiContext* context = ImGui::CreateContext();
		ImFont* font = nullptr;

		for (const std::filesystem::path& path : EditorTheme::EmojiFonts())
		{
			ImFontConfig config;
			config.MergeMode = font != nullptr;

			font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), EditorTheme::DefaultFontSize, &config);
		};

		REQUIRE(font != nullptr);

		for (const char* label : labels)
		{
			const char* end = label + std::strlen(label);

			for (const char* at = label; at < end;)
			{
				unsigned int codepoint = 0;
				at += ImTextCharFromUtf8(&codepoint, at, end);

				INFO(label);
				CAPTURE(codepoint);
				CHECK((codepoint < first_symbol || font->IsGlyphInFont((ImWchar)codepoint)));
			};
		};

		ImGui::DestroyContext(context);
	};
};

#endif

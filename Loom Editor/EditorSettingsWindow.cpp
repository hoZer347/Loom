#include "EditorSettings.h"

#include "Engine.h"

#include "imgui.h"


namespace Loom
{
	namespace
	{
		constexpr ImVec2 WINDOW_SIZE = ImVec2(460.0f, 360.0f);

		const char* const THEME_LABELS[] = { "Loom", "ImGui Dark", "ImGui Light", "ImGui Classic" };

		const char* const PREVIEW_TEXT = "The quick brown fox jumps over the lazy dog. 0123456789";
	};

	void EditorSettings::Apply() const
	{
		EditorTheme::LoadFont(font, fontSize);
		ApplyLook();
	};

	void EditorSettings::ApplyLook() const
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style = ImGuiStyle();

		switch (theme)
		{
		case Theme::Dark:		ImGui::StyleColorsDark();		break;
		case Theme::Light:		ImGui::StyleColorsLight();		break;
		case Theme::Classic:	ImGui::StyleColorsClassic();	break;
		default:				EditorTheme::ApplyStyle();		break;
		};

		// A panel dragged out of the window is a window of its own, and looks
		// like the docked ones: square and opaque.
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		};

		style.ScaleAllSizes(scale);
		ImGui::GetIO().FontGlobalScale = scale;
	};

	void EditorSettings::QueueFont() const
	{
		Engine::QueueTask(
			[file = font, size = fontSize]()
			{
				EditorTheme::LoadFont(file, size);

				if (Renderer* renderer = Renderer::Get())
					renderer->ReleaseImGuiFonts();
			});
	};

	void EditorSettings::QueueLook() const
	{
		Engine::QueueTask([look = *this]() { look.ApplyLook(); });
	};

	bool EditorSettings::Draw(bool* open)
	{
		bool changed = false;

		ImGui::SetNextWindowSize(WINDOW_SIZE, ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Settings", open) && ImGui::BeginTabBar("Settings Tabs"))
		{
			if (ImGui::BeginTabItem("Graphics"))
			{
				changed |= DrawGraphics();
				ImGui::EndTabItem();
			};

			if (ImGui::BeginTabItem("Fonts"))
			{
				changed |= DrawFonts();
				ImGui::EndTabItem();
			};

			if (ImGui::BeginTabItem("Interface"))
			{
				changed |= DrawInterface();
				ImGui::EndTabItem();
			};

			ImGui::EndTabBar();
		};

		ImGui::End();

		return changed;
	};

	bool EditorSettings::DrawGraphics()
	{
		bool changed = false;

		ImGui::Text("Running on %s", BackendLabel(Engine::backend));
		ImGui::Separator();

		for (const Backend option : { Backend::OpenGL, Backend::Vulkan })
			if (ImGui::RadioButton(BackendLabel(option), backend == option) && backend != option)
			{
				backend = option;
				changed = true;
			};

		if (backend != Engine::backend)
		{
			ImGui::BeginDisabled();
			ImGui::TextWrapped("Takes effect when the editor restarts.");
			ImGui::EndDisabled();
		};

		return changed;
	};

	bool EditorSettings::DrawFonts()
	{
		bool changed = false;

		if (ImGui::BeginCombo("Font", font.c_str()))
		{
			for (const std::string& option : EditorTheme::Fonts())
				if (ImGui::Selectable(option.c_str(), option == font) && option != font)
				{
					font = option;
					changed = true;
				};

			ImGui::EndCombo();
		};

		ImGui::SliderFloat("Size", &fontSize, EditorTheme::MinFontSize, EditorTheme::MaxFontSize, "%.0f px");

		// On release: every change rebuilds the font texture.
		changed |= ImGui::IsItemDeactivatedAfterEdit();

		if (changed)
			QueueFont();

		ImGui::Separator();
		ImGui::TextWrapped("%s", PREVIEW_TEXT);

		return changed;
	};

	bool EditorSettings::DrawInterface()
	{
		bool changed = false;

		if (ImGui::BeginCombo("Theme", THEME_LABELS[(int)theme]))
		{
			for (int i = 0; i < (int)Theme::Count; i++)
				if (ImGui::Selectable(THEME_LABELS[i], i == (int)theme) && i != (int)theme)
				{
					theme = (Theme)i;
					changed = true;
				};

			ImGui::EndCombo();
		};

		ImGui::SliderFloat("Scale", &scale, MinScale, MaxScale, "%.2fx");
		changed |= ImGui::IsItemDeactivatedAfterEdit();

		if (changed)
			QueueLook();

		// ImGui's own editor, for everything else. What it changes lasts until
		// the theme or scale is applied again.
		if (ImGui::CollapsingHeader("Style editor (this session only)"))
			ImGui::ShowStyleEditor();

		return changed;
	};
};

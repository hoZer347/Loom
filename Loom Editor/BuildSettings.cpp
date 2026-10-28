#include "BuildSettings.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "EditorIcons.h"
#include "ProjectTemplate.h"
#include "ScriptRun.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <fstream>


namespace Loom
{
	namespace
	{
		const char* const configuration_key = "configuration";
		const char* const scene_key = "scene";
		const char* const folder_key = "folder";

		// Beside the project folder, as "<game> Build".
		const char* const folder_suffix = " Build";

		const char* const program_extension = ".exe";

		constexpr ImVec2 window_size = ImVec2(640.0f, 0.0f);
	};

	#pragma region File

	void BuildSettings::Load(const std::string& path)
	{
		std::ifstream in(path);
		std::string line;

		while (std::getline(in, line))
		{
			while (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (const std::string value = ProjectTemplate::ValueOf(line, configuration_key); IsConfiguration(value))
				configuration = value;

			if (const std::string value = ProjectTemplate::ValueOf(line, scene_key); !value.empty())
				scene = value;

			if (const std::string value = ProjectTemplate::ValueOf(line, folder_key); !value.empty())
				folder = value;
		};

		m_history.Reset(Current());
	};

	bool BuildSettings::Save(const std::string& path)
	{
		std::ofstream out(path, std::ios::binary);

		out << configuration_key << '=' << configuration << '\n';

		if (!scene.empty())
			out << scene_key << '=' << scene << '\n';

		if (!folder.empty())
			out << folder_key << '=' << folder << '\n';

		if (!out)
			return false;

		m_history.Saved(Current());

		return true;
	};

	#pragma endregion

	#pragma region History

	void BuildSettings::DefaultScene(const std::string& scene)
	{
		this->scene = scene;
		m_history.Amend(Current());
	};

	bool BuildSettings::Shortcuts()
	{
		const bool undo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z);
		const bool redo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);

		if (undo || redo)
			if (Settings settings = Current(); m_history.Step(settings, undo))
			{
				configuration = settings.configuration;
				scene = settings.scene;
				folder = settings.folder;
			};

		return ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S);
	};

	#pragma endregion

	#pragma region Paths

	std::filesystem::path BuildSettings::DefaultOutput(const std::string& project, const std::string& name)
	{
		return (std::filesystem::path(project).parent_path() / (name + folder_suffix)).lexically_normal();
	};

	std::filesystem::path BuildSettings::Output(const std::string& project, const std::string& name) const
	{
		if (folder.empty())
			return DefaultOutput(project, name);

		const std::filesystem::path chosen(folder);

		return (chosen.is_absolute() ? chosen : std::filesystem::path(project) / chosen).lexically_normal();
	};

	std::filesystem::path BuildSettings::Program(const std::string& project, const std::string& name) const
	{
		return Output(project, name) / (name + program_extension);
	};

	std::string BuildSettings::Arguments(const std::string& project, const std::string& name) const
	{
		const std::filesystem::path start = std::filesystem::path(scene).is_absolute()
			? std::filesystem::path(scene)
			: std::filesystem::path(project) / scene;

		return "-Project " + ScriptRun::Quote(project) +
			" -Scene " + ScriptRun::Quote(start.lexically_normal().string()) +
			" -Configuration " + configuration +
			" -Output " + ScriptRun::Quote(Output(project, name).string());
	};

	#pragma endregion

	#pragma region Window

	BuildSettings::Action BuildSettings::Draw(
		bool* open,
		const std::string& project,
		const std::string& name,
		const std::function<std::vector<std::string>()>& list_scenes,
		std::string_view status,
		bool building)
	{
		Action action = Action::None;

		ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);

		m_focused = false;

		if (!ImGui::Begin(EditorPanels::BuildSettings, open, Unsaved() ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None))
		{
			ImGui::End();
			return action;
		};

		if (!project.empty())
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (Shortcuts())
				action = Action::Save;
		};

		ImGui::BeginDisabled(project.empty());

		if (ImGui::BeginCombo("Configuration", configuration.c_str()))
		{
			for (const std::string_view option : configurations)
				if (ImGui::Selectable(std::string(option).c_str(), option == configuration) && option != configuration)
				{
					configuration = option;
					Keep();
				};

			ImGui::EndCombo();
		};

		if (ImGui::BeginCombo("Scene", scene.c_str()))
		{
			if (ImGui::IsWindowAppearing())
				m_scenes = list_scenes();

			for (const std::string& option : m_scenes)
				if (ImGui::Selectable(option.c_str(), option == scene) && option != scene)
				{
					scene = option;
					Keep();
				};

			ImGui::EndCombo();
		};

		// Left alone while typed in, else following the folder the browser chose.
		const ImGuiID folder_id = ImGui::GetID("##Folder");

		if (ImGui::GetActiveID() != folder_id)
			snprintf(m_folder, sizeof(m_folder), "%s", folder.c_str());

		const float browse_width = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;

		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browse_width - ImGui::GetStyle().ItemInnerSpacing.x);

		const std::string hint = project.empty() ? "" : DefaultOutput(project, name).string();

		if (ImGui::InputTextWithHint("##Folder", hint.c_str(), m_folder, sizeof(m_folder)))
			folder = m_folder;

		if (ImGui::IsItemDeactivatedAfterEdit())
			Keep();

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

		if (ImGui::Button("Browse..."))
			action = Action::Browse;

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted("Folder");

		ImGui::Separator();

		ImGui::BeginDisabled(building || scene.empty());

		if (ImGui::Button("Build"))
			action = Action::Build;

		ImGui::SameLine();

		if (ImGui::Button("Build and Run"))
			action = Action::BuildAndRun;

		ImGui::EndDisabled();
		ImGui::EndDisabled();

		if (!status.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%.*s", (int)status.size(), status.data());
		};

		ImGui::End();

		return action;
	};

	#pragma endregion
};

#endif

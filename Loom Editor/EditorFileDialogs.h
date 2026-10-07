#pragma once

#include "ProjectTemplate.h"
#include "SceneSerializer.h"

#include "imgui.h"
#include "imfilebrowser.h"

// The file browser pulls in windows.h for the drive list, and windows.h turns
// SetCurrentDirectory into SetCurrentDirectoryW behind our back.
#ifdef SetCurrentDirectory
#undef SetCurrentDirectory
#endif


namespace Loom
{
	// One browser per job, so each remembers where it was last pointed.
	struct EditorFileDialogs final
	{
		ImGui::FileBrowser openProject{ ImGuiFileBrowserFlags_CloseOnEsc };

		ImGui::FileBrowser saveScene{
			ImGuiFileBrowserFlags_EnterNewFilename |
			ImGuiFileBrowserFlags_CreateNewDir |
			ImGuiFileBrowserFlags_CloseOnEsc };

		ImGui::FileBrowser folder{
			ImGuiFileBrowserFlags_SelectDirectory |
			ImGuiFileBrowserFlags_CreateNewDir |
			ImGuiFileBrowserFlags_CloseOnEsc };

		ImGui::FileBrowser newProjectFolder{
			ImGuiFileBrowserFlags_SelectDirectory |
			ImGuiFileBrowserFlags_CreateNewDir |
			ImGuiFileBrowserFlags_CloseOnEsc };

		EditorFileDialogs()
		{
			openProject.SetTitle("Open Project");
			openProject.SetTypeFilters({ ProjectTemplate::extension });

			saveScene.SetTitle("Save Scene As");
			saveScene.SetTypeFilters({ SceneSerializer::extension });

			folder.SetTitle("Choose a Scene Folder");

			newProjectFolder.SetTitle("Where should the project go?");
		};
	};
};

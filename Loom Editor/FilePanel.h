#pragma once

#include "EditorIcons.h"
#include "FolderTree.h"
#include "KindFilter.h"
#include "VirtualFolders.h"

#include <string>
#include <vector>


namespace Loom
{
	// What sets one FilePanel apart from the others.
	struct FilePanelKind
	{
		// Its title in EditorPanels, what the console calls it, what its empty
		// list says it has none of, and its folders' file's icon.
		const char* title;
		const char* name;
		const char* files;
		const char* icon;

		// What its KindFilter shows: the icon beside each of its files.
		KindFilter::Kind filter;

		// Its folders' file beside the .loomproject, the start of the lines
		// that file files each of its files with, and what a folder dragged in
		// it carries.
		const char* extension;
		const char* itemPrefix;
		const char* folderPayload;
	};

	namespace FilePanels
	{
		inline constexpr FilePanelKind TwoD{ EditorPanels::TwoD, "2D", "textures, sprites or palettes", EditorIcons::TwoD, { EditorIcons::Texture, "Textures, sprites and palettes" }, ".loom2d", "2d.", "Loom2DFolder" };
		inline constexpr FilePanelKind ThreeD{ EditorPanels::ThreeD, "3D", "models", EditorIcons::ThreeD, { EditorIcons::Model, "Models" }, ".loom3d", "3d.", "Loom3DFolder" };
		inline constexpr FilePanelKind Sounds{ EditorPanels::Sounds, "Sounds", "sounds", EditorIcons::Sound, { EditorIcons::Sound, "Sounds" }, ".loomsounds", "sound.", "LoomSoundFolder" };
		inline constexpr FilePanelKind Fonts{ EditorPanels::Fonts, "Fonts", "fonts", EditorIcons::Font, { EditorIcons::Font, "Fonts" }, ".loomfonts", "font.", "LoomFontFolder" };

		inline constexpr const FilePanelKind* all[] = { &TwoD, &ThreeD, &Sounds, &Fonts };
	};

	/**
	* Loom::FilePanel
	* - A panel holding the project's files of one kind, which the Project
	*   panel leaves to it: the 2D panel its textures, sprites and palettes,
	*   and the 3D, Sounds and Fonts panels theirs
	* - Its files are filed in folders of its own by their paths in the
	*   project, apart from the folders they are in, so filing one moves no
	*   file. A project keeps them beside its .loomproject in a file of the
	*   panel's extension, as the Scripts panel's are
	*/
	struct FilePanel final
	{
		explicit FilePanel(const FilePanelKind* kind) : kind(*kind) { };

		// Empty when the file is not there yet.
		void Load(const std::string& file) { folders.Load(file, kind.itemPrefix); };
		bool Save(const std::string& file) const { return folders.Save(file, kind.itemPrefix); };

		const FilePanelKind& kind;

		// From the last scan, each by its path in the project, as fields name it.
		std::vector<std::string> files{ };
		VirtualFolders folders;
		FolderTree tree;
		KindFilter filter;
		std::string selected{ };
		bool reveal = false;

		// Where the Create prompt's file is filed once it is written.
		std::string newFolder{ };

		bool show = true;
		bool focus = false;
	};
};

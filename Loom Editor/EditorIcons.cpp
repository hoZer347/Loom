#include "EditorIcons.h"

#include "FilePanel.h"
#include "InputActions.h"
#include "Prefab.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "SceneSerializer.h"
#include "ScriptFolders.h"
#include "SpriteAsset.h"

#include <algorithm>
#include <cctype>
#include <filesystem>


namespace Loom
{
	const char* EditorIcons::Of(const std::string& path)
	{
		std::string extension = std::filesystem::path(path).extension().string();

		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });

		if (extension == SceneSerializer::extension)
			return Scene;

		if (extension == Loom::Prefab::extension)
			return Prefab;

		if (extension == ProjectAssets::shaderExtension)
			return Shader;

		if (extension == ProjectTemplate::extension)
			return Project;

		if (extension == ScriptFolders::extension)
			return Script;

		for (const FilePanelKind* kind : FilePanels::all)
			if (extension == kind->extension)
				return kind->icon;

		if (extension == InputActions::extension)
			return Bindings;

		if (extension == SpriteAsset::extension)
			return Sprite;

		if (extension == Loom::Palette::extension)
			return Palette;

		if (ProjectAssets::IsTextureFile(path))
			return Texture;

		if (ProjectAssets::IsSoundFile(path))
			return Sound;

		if (ProjectAssets::IsFontFile(path))
			return Font;

		return File;
	};
};

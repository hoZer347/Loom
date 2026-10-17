#include "EditorIcons.h"

#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "SceneSerializer.h"
#include "ScriptFolders.h"

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

		if (extension == ProjectAssets::shaderExtension)
			return Shader;

		if (extension == ProjectTemplate::extension)
			return Project;

		if (extension == ScriptFolders::extension)
			return Script;

		if (ProjectAssets::IsTextureFile(path))
			return Texture;

		if (ProjectAssets::IsSoundFile(path))
			return Sound;

		return File;
	};
};

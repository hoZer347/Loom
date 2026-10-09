#include "EditorIcons.h"

#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>


namespace Loom
{
	namespace
	{
		// The other script extensions are headers.
		constexpr const char* sourceExtension = ".cpp";
	};

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

		if (ProjectAssets::IsTextureFile(path))
			return Texture;

		if (ProjectAssets::IsSoundFile(path))
			return Sound;

		if (ProjectAssets::IsScriptFile(path))
			return extension == sourceExtension ? Source : Header;

		return File;
	};
};

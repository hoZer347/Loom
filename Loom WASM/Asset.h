#pragma once

#include "Macro Helpers.h"

#include <array>
#include <string>
#include <utility>


namespace Loom
{
	/**
	* Loom::Asset
	* - A file in the project, named by its path from the project folder with
	*   '/' between folders, and a std::string of that path to everything that
	*   reads it: Serial<Asset<".shader">> m_shader = "Assets/Lit.shader"
	* - Takes every extension its files may end in, first the one it is filed
	*   under: Asset<".ttf", ".otf", ".ttc">. A kind of file with several, such
	*   as FontAsset, is named beside what loads it
	* - The editor's inspector draws it as the file it names, which a click
	*   swaps for another of the project's files ending in one of extensions,
	*   picked from a list of them as the Project panel files them, or by
	*   their paths where no tab of it holds them
	* - Written to scenes as the string it is, so a Serial<std::string> that
	*   becomes one keeps the values already saved
	*/
	template <Str... extensions>
	struct Asset final : std::string
	{
		static_assert(sizeof...(extensions) > 0, "An Asset names the extensions of its files");

		using std::string::string;

		Asset() = default;
		Asset(std::string path) : std::string(std::move(path)) { };

		static constexpr std::array<const char*, sizeof...(extensions)> fileExtensions{ extensions.value... };
	};

	/**
	* Loom::Folder
	* - A folder in the project, named by its path as an Asset is:
	*   Serial<Folder> m_folder = "Assets/Prefabs"
	* - An Asset of no extensions, which the inspector draws as a folder,
	*   picked from the project's folders or dropped from the Project panel
	*/
	struct Folder final : std::string
	{
		using std::string::string;

		Folder() = default;
		Folder(std::string path) : std::string(std::move(path)) { };

		static constexpr std::array<const char*, 0> fileExtensions{ };
	};
};

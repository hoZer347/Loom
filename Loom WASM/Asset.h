#pragma once

#include "Macro Helpers.h"

#include <string>
#include <utility>


namespace Loom
{
	/**
	* Loom::Asset
	* - A file in the project, named by its path from the project folder with
	*   '/' between folders, and a std::string of that path to everything that
	*   reads it: Serial<Asset<".shader">> m_shader = "Assets/Lit.shader"
	* - The editor's inspector draws it as the file it names, which a click
	*   swaps for another of the project's files ending in extension, picked
	*   from a list of them as the Project panel files them, or by their
	*   paths where no tab of it holds them
	* - Written to scenes as the string it is, so a Serial<std::string> that
	*   becomes one keeps the values already saved
	*/
	template <Str extension>
	struct Asset final : std::string
	{
		using std::string::string;

		Asset() = default;
		Asset(std::string path) : std::string(std::move(path)) { };

		static constexpr const char* fileExtension = extension.value;
	};
};

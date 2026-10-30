#pragma once

#include "Loom API.h"

#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::BuiltInShaders
	* - The shaders every project has, compiled into the engine so they need no
	*   file on disk, in the editor, a built game or on the web
	* - A Material names one by its path, as it would a project's file:
	*   Loom/Lit.shader, Loom/Unlit.shader, Loom/LitTextured.shader,
	*   Loom/UnlitTextured.shader or Loom/Transparent.shader
	* - The Loom folder is the engine's, so a project's own Loom/Lit.shader is
	*   never read
	*/
	struct LOOM_API BuiltInShaders final
	{
		// What a project with no shader of its own draws with, and what a new
		// shader file starts from.
		static constexpr const char* Lit = "Loom/Lit.shader";

		// Every one's path, for a list to pick from.
		static const std::vector<std::string>& Paths();

		// The source of the one at path, or nullptr for any other path.
		static const char* SourceOf(const std::string& path);
	};
};

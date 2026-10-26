#pragma once

#include <string>


namespace Loom
{
	inline constexpr const char* ellipsis = "...";

	// As much of the end of text as fits in width in ImGui's current font,
	// behind an ellipsis, or all of it if it fits. Never cuts a character
	// UTF-8 spells in several bytes.
	std::string FitTail(const std::string& text, float width);

	// As FitTail, but cut only at a separator between a path's folders while
	// its file's name still fits, so the cut leaves no part of a name.
	std::string FitPath(const std::string& path, float width);
};

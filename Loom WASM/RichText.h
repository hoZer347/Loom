#pragma once

#include "Loom API.h"

#include "glm/glm.hpp"

#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::RichText
	* - Text with tags written as [Name, arg] in it, which a UiText draws and a
	*   DialogueManager passes through rather than runs
	* - [Color, red] or [Color, #FF8000] or [Color, #FF800080] colours the text
	*   after it until [/Color]; [Size, 32] draws it at that many pixels until
	*   [/Size]. Each closing tag goes back to what was in place before its
	*   opening one, so tags nest
	* - A tag's name is matched whatever its case. A bracket that is not a tag
	*   is text, drawn as it is written
	*/
	struct LOOM_API RichText final
	{
		RichText() = delete;

		// One run of text drawn the same way throughout.
		struct Span
		{
			std::string text;
			glm::vec4 color{ 1.0f };
			float size = 0.0f;
		};

		static constexpr std::string_view ColorTag = "Color";
		static constexpr std::string_view SizeTag = "Size";
		static constexpr char Close = '/';

		// Every tag's name, opening and closing, as a script writes it.
		static const std::vector<std::string>& Tags();

		// Whether token, brackets and all, is one of the tags.
		static bool IsTag(const std::string& token);

		// The text cut where its tags change how it is drawn, the tags left out,
		// starting in color at size.
		static std::vector<Span> Spans(const std::string& text, const glm::vec4& color, float size);
	};
};

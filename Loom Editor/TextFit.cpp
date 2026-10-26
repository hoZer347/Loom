#include "TextFit.h"

#include "imgui.h"
#include "imgui_internal.h"


namespace Loom
{
	std::string FitTail(const std::string& text, float width)
	{
		const char* at = text.c_str();
		const char* const end = at + text.size();

		if (ImGui::CalcTextSize(at, end).x <= width)
			return text;

		const float room = width - ImGui::CalcTextSize(ellipsis).x;

		while (at < end)
		{
			unsigned int c = 0;
			at += ImTextCharFromUtf8(&c, at, end);

			if (ImGui::CalcTextSize(at, end).x <= room)
				return ellipsis + std::string(at, end);
		};

		return ellipsis;
	};

	std::string FitPath(const std::string& path, float width)
	{
		constexpr const char* separators = "/\\";

		if (ImGui::CalcTextSize(path.c_str()).x <= width)
			return path;

		const float room = width - ImGui::CalcTextSize(ellipsis).x;

		for (size_t at = path.find_first_of(separators); at != std::string::npos; at = path.find_first_of(separators, at + 1))
			if (ImGui::CalcTextSize(path.c_str() + at).x <= room)
				return ellipsis + path.substr(at);

		return FitTail(path, width);
	};
};

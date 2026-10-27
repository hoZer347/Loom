#include "RichText.h"

#include "Utilities/DialogueArgs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr char OPEN_BRACKET = '[';
		constexpr char CLOSE_BRACKET = ']';
		constexpr char ARGUMENT = ',';
		constexpr char HEX = '#';

		constexpr int HEX_BASE = 16;
		constexpr float CHANNEL_MAX = 255.0f;

		// #RRGGBB, or #RRGGBBAA, after the #.
		constexpr size_t RGB_DIGITS = 6;
		constexpr size_t RGBA_DIGITS = 8;
		constexpr size_t CHANNEL_DIGITS = 2;

		constexpr glm::vec4 ORANGE = { 1.0f, 0.5f, 0.0f, 1.0f };
		constexpr glm::vec4 GREY = { 0.5f, 0.5f, 0.5f, 1.0f };

		const std::pair<std::string_view, glm::vec4> NAMED_COLORS[] =
		{
			{ "white", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) },
			{ "black", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) },
			{ "red", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) },
			{ "green", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) },
			{ "blue", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) },
			{ "yellow", glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) },
			{ "cyan", glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) },
			{ "magenta", glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) },
			{ "orange", ORANGE },
			{ "grey", GREY },
			{ "gray", GREY },
		};

		enum class Kind { Color, EndColor, Size, EndSize };

		struct Tag
		{
			Kind kind;
			glm::vec4 color{ 1.0f };
			float size = 0.0f;
		};

		std::string Lowered(std::string_view text)
		{
			std::string lowered(text);

			std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return (char)std::tolower(c); });

			return lowered;
		};

		std::optional<glm::vec4> ReadColor(const std::string& written)
		{
			const std::string lowered = Lowered(written);

			for (const auto& [name, color] : NAMED_COLORS)
				if (lowered == name)
					return color;

			const size_t digits = written.size() - 1;

			if (written.empty() || written.front() != HEX || (digits != RGB_DIGITS && digits != RGBA_DIGITS))
				return std::nullopt;

			if (!std::all_of(written.begin() + 1, written.end(), [](unsigned char c) { return std::isxdigit(c); }))
				return std::nullopt;

			glm::vec4 color{ 1.0f };

			for (size_t channel = 0; channel < digits / CHANNEL_DIGITS; channel++)
				color[(int)channel] = (float)std::strtol(written.substr(1 + channel * CHANNEL_DIGITS, CHANNEL_DIGITS).c_str(), nullptr, HEX_BASE) / CHANNEL_MAX;

			return color;
		};

		// The tag token names, brackets and all, or nothing when it is not one
		// or its argument will not read.
		std::optional<Tag> Parse(const std::string& token)
		{
			if (token.size() < 2 || token.front() != OPEN_BRACKET || token.back() != CLOSE_BRACKET)
				return std::nullopt;

			const std::string content = token.substr(1, token.size() - 2);
			const size_t comma = content.find(ARGUMENT);
			const std::string name = Lowered(DialogueArgs::Trim(content.substr(0, comma)));
			const std::vector<std::string> args = comma == std::string::npos
				? std::vector<std::string>{ }
				: DialogueArgs::Split(content.substr(comma + 1));

			if (args.empty())
			{
				if (name == RichText::Close + Lowered(RichText::ColorTag))
					return Tag{ Kind::EndColor };

				if (name == RichText::Close + Lowered(RichText::SizeTag))
					return Tag{ Kind::EndSize };

				return std::nullopt;
			};

			if (args.size() != 1)
				return std::nullopt;

			if (name == Lowered(RichText::ColorTag))
				if (const std::optional<glm::vec4> color = ReadColor(DialogueArgs::Unquote(args.front())))
					return Tag{ Kind::Color, *color };

			if (float size = 0.0f; name == Lowered(RichText::SizeTag) && DialogueArgs::TryCoerce(args.front(), size) && size > 0.0f)
				return Tag{ Kind::Size, glm::vec4(1.0f), size };

			return std::nullopt;
		};
	};

	const std::vector<std::string>& RichText::Tags()
	{
		static const std::vector<std::string> tags =
		{
			std::string(ColorTag),
			Close + std::string(ColorTag),
			std::string(SizeTag),
			Close + std::string(SizeTag),
		};

		return tags;
	};

	bool RichText::IsTag(const std::string& token)
	{
		return Parse(token).has_value();
	};

	std::vector<RichText::Span> RichText::Spans(const std::string& text, const glm::vec4& color, float size)
	{
		std::vector<Span> spans{ };
		std::vector<glm::vec4> colors{ color };
		std::vector<float> sizes{ size };
		std::string run{ };

		const auto flush = [&]()
		{
			if (!run.empty())
				spans.push_back({ std::move(run), colors.back(), sizes.back() });

			run.clear();
		};

		for (size_t at = 0; at < text.size(); )
		{
			const size_t close = text[at] == OPEN_BRACKET ? text.find(CLOSE_BRACKET, at) : std::string::npos;
			const std::optional<Tag> tag = close == std::string::npos ? std::nullopt : Parse(text.substr(at, close - at + 1));

			if (!tag)
			{
				run += text[at++];
				continue;
			};

			flush();

			// A closing tag with nothing open is dropped, leaving what the text started in.
			if (tag->kind == Kind::Color)
				colors.push_back(tag->color);
			else if (tag->kind == Kind::Size)
				sizes.push_back(tag->size);
			else if (tag->kind == Kind::EndColor && colors.size() > 1)
				colors.pop_back();
			else if (tag->kind == Kind::EndSize && sizes.size() > 1)
				sizes.pop_back();

			at = close + 1;
		};

		flush();

		return spans;
	};
};

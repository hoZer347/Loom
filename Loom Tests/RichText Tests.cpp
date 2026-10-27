#include "doctest.h"

#include "RichText.h"

#include "glm/glm.hpp"

#include <string>
#include <vector>

namespace
{
	constexpr glm::vec4 WHITE = { 1.0f, 1.0f, 1.0f, 1.0f };
	constexpr glm::vec4 RED = { 1.0f, 0.0f, 0.0f, 1.0f };
	constexpr glm::vec4 YELLOW = { 1.0f, 1.0f, 0.0f, 1.0f };
	constexpr float SIZE = 24.0f;
	constexpr float BIG = 32.0f;

	std::vector<Loom::RichText::Span> Spans(const std::string& text)
	{
		return Loom::RichText::Spans(text, WHITE, SIZE);
	};
};


TEST_SUITE("RichText")
{
	TEST_CASE("text with no tags is one span in the colour and size it starts in")
	{
		const std::vector<Loom::RichText::Span> spans = Spans("Hello there");

		REQUIRE(spans.size() == 1);
		CHECK(spans[0].text == "Hello there");
		CHECK(spans[0].color == WHITE);
		CHECK(spans[0].size == SIZE);
	};

	TEST_CASE("a colour tag colours the text after it until its closing tag")
	{
		const std::vector<Loom::RichText::Span> spans = Spans("A [Color, red]red[/Color] word");

		REQUIRE(spans.size() == 3);
		CHECK(spans[0].text == "A ");
		CHECK(spans[0].color == WHITE);
		CHECK(spans[1].text == "red");
		CHECK(spans[1].color == RED);
		CHECK(spans[2].text == " word");
		CHECK(spans[2].color == WHITE);
	};

	TEST_CASE("a size tag sizes the text after it until its closing tag")
	{
		const std::vector<Loom::RichText::Span> spans = Spans("[Size, 32]Big[/Size] small");

		REQUIRE(spans.size() == 2);
		CHECK(spans[0].text == "Big");
		CHECK(spans[0].size == BIG);
		CHECK(spans[1].size == SIZE);
	};

	TEST_CASE("tags nest, each closing one going back to what was in place before it")
	{
		const std::vector<Loom::RichText::Span> spans = Spans("[Color, red]a[Color, yellow]b[Size, 32]c[/Size][/Color]d[/Color]e");

		REQUIRE(spans.size() == 5);
		CHECK(spans[0].color == RED);
		CHECK(spans[1].color == YELLOW);
		CHECK(spans[2].color == YELLOW);
		CHECK(spans[2].size == BIG);
		CHECK(spans[3].text == "d");
		CHECK(spans[3].color == RED);
		CHECK(spans[3].size == SIZE);
		CHECK(spans[4].color == WHITE);
	};

	TEST_CASE("a colour is a name or a hex code, with or without alpha")
	{
		constexpr float HALF_CHANNEL = 128.0f / 255.0f;
		constexpr float CLOSE = 0.001f;

		const std::vector<Loom::RichText::Span> spans = Spans("[Color, #FF0000]a[Color, #00FF0080]b[COLOR, Yellow]c");

		REQUIRE(spans.size() == 3);
		CHECK(spans[0].color == RED);
		CHECK(spans[1].color.g == 1.0f);
		CHECK(std::abs(spans[1].color.a - HALF_CHANNEL) < CLOSE);
		CHECK(spans[2].color == YELLOW);
	};

	TEST_CASE("a bracket that is not a tag is text, drawn as it is written")
	{
		for (const char* text : { "[Pause]", "[Color, nope]", "[Size, -4]", "[Size]", "[Color, red, blue]", "a [ b" })
		{
			CAPTURE(text);

			CHECK_FALSE(Loom::RichText::IsTag(text));

			const std::vector<Loom::RichText::Span> spans = Spans(text);

			REQUIRE(spans.size() == 1);
			CHECK(spans[0].text == text);
		};
	};

	TEST_CASE("a closing tag with nothing open is dropped")
	{
		const std::vector<Loom::RichText::Span> spans = Spans("a[/Color]b");

		REQUIRE(spans.size() == 2);
		CHECK(spans[0].color == WHITE);
		CHECK(spans[1].color == WHITE);
		CHECK(spans[0].text + spans[1].text == "ab");
	};

	TEST_CASE("a tag is told apart by its name, whatever its case")
	{
		CHECK(Loom::RichText::IsTag("[Color, red]"));
		CHECK(Loom::RichText::IsTag("[/size]"));
	};
};

#include "doctest.h"

// The editor's text panels are not in the web build.
#ifndef __EMSCRIPTEN__

#include "Test Support.h"

#include "DialogueEditor.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	// What the command at the start of text is drawn as, and how much of it.
	struct Coloured
	{
		size_t length = 0;
		TextEditor::Color color = TextEditor::Color::text;
	};

	Coloured Command(const std::string& text)
	{
		Coloured coloured;
		coloured.length = Loom::DialogueEditor::Command(text, coloured.color);
		return coloured;
	};
};


TEST_SUITE("DialogueEditor")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the commands are a DialogueManager's bindings and the RichText tags, sorted")
	{
		const std::vector<std::string>& commands = Loom::DialogueEditor::Commands();

		for (const char* name : { "Proceed", "Clear", "Pause", "TextSpeed", "Color", "/Color", "Size", "/Size" })
		{
			CAPTURE(name);
			CHECK(std::find(commands.begin(), commands.end(), name) != commands.end());
		};

		CHECK(std::is_sorted(commands.begin(), commands.end()));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a RichText tag is coloured as a declaration, brackets and all")
	{
		const Coloured tag = Command("[Color, red]hello");

		CHECK(tag.length == std::string("[Color, red]").size());
		CHECK(tag.color == TextEditor::Color::declaration);

		CHECK(Command("[/Color] after").length == std::string("[/Color]").size());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a binding is coloured as a keyword, whatever its case and arguments")
	{
		const Coloured pause = Command("[pause] then");

		CHECK(pause.length == std::string("[pause]").size());
		CHECK(pause.color == TextEditor::Color::keyword);

		const Coloured speed = Command("[TextSpeed, 0.1]");

		CHECK(speed.length == std::string("[TextSpeed, 0.1]").size());
		CHECK(speed.color == TextEditor::Color::keyword);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a bracket naming no command, or never closed, is left as text")
	{
		for (const char* text : { "[Shout]", "[Pause", "Pause]", "plain", "" })
		{
			CAPTURE(text);
			CHECK(Command(text).length == 0);
		};
	};
};

#endif

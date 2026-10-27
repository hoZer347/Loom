#pragma once

#include "Loom.h"

#include <algorithm>
#include <cctype>
#include <string>


namespace DialogueScripts
{
	// The Dialogue Box's manager, whose DialogueBinding members are commands
	// Intro.dialogue can run beside the ones every DialogueManager has.
	struct Narrator : Loom::DialogueManager<Narrator>
	{
		// A DialogueBinding is a command this manager's .dialogue scripts can run.
		// [Name, 1, 2] in a script calls the binding named Name with 1 and 2 when
		// the dialogue reaches it. Its arguments are read as the types listed, so
		// declaring the same name again with other types overloads it.
		Loom::DialogueBinding<"Shout", std::string> shout = [this](const std::string& word)
		{
			std::string loud = word;
			std::ranges::transform(loud, loud.begin(), [](unsigned char c) { return (char)std::toupper(c); });
			Append(loud + '!');
		};
	};
};

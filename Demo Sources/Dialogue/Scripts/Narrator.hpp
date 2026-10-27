#pragma once

#include "Loom.h"

#include <algorithm>
#include <cctype>
#include <string>


namespace DialogueScripts
{
	// The Dialogue Box's manager, whose LOOM_DIALOGUE_BINDING functions are
	// commands Intro.dialogue can run beside the ones every DialogueManager has.
	struct Narrator : Loom::DialogueManager<Narrator>
	{
		// LOOM_DIALOGUE_BINDING(Name) makes the function after it a command this
		// manager's .dialogue scripts can run. [Name, 1, 2] in a script calls it
		// with 1 and 2 when the dialogue reaches it, read as the types it takes.
		// To give a name several forms, declare each as a Loom::DialogueBinding.
		LOOM_DIALOGUE_BINDING(Shout)(const std::string& word)
		{
			std::string loud = word;
			std::ranges::transform(loud, loud.begin(), [](unsigned char c) { return (char)std::toupper(c); });
			Append(loud + '!');
		};
	};
};

#pragma once

#include "Engine.h"


// Every case shares the page's one canvas, so the engine is made once.
inline void OpenPage()
{
	static Loom::Engine engine;
};

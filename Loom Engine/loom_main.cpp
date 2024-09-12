#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include "imgui.h"

import Engine;
import Scene;
import Geometry;

using namespace Loom;

#include <glm/glm.hpp>
using namespace glm;


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	Scene scene0 { };
	Scene scene1 { };
	Scene scene2 { };
	Scene scene3 { };

	scene0.Attach<Geometry::Rect>(100.0f, 100.0f, 100.0f, 100.0f, vec4(0, 0.5f, 0, 1));
	scene1.Attach<Geometry::Rect>(300.0f, 100.0f, 100.0f, 100.0f, vec4(0.5f, 0, 0, 1));

	Engine::Start("C:/Desktop/Loom Project 1/");

	return 0;
};

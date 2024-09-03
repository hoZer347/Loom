import Scene;
import Engine;
import Buffer;
import Geometry;
import Component;
import GameObject;

using namespace Loom;

#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include <glm/glm.hpp>
using namespace glm;


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	Engine engine{ };

	Scene scene0 { &engine, "Scene 1" };
	Scene scene1 { &engine, "Scene 2" };
	Scene scene2 { &engine, "Scene 3" };
	Scene scene3 { &engine, "Scene 4" };

	scene0.Attach<Rect>(100.0f, 100.0f, 100.0f, 100.0f, vec4(0, 0.5f, 0, 1));
	scene1.Attach<Rect>(300.0f, 100.0f, 100.0f, 100.0f, vec4(0.5f, 0, 0, 1));

	engine.Start();

	return 0;
};

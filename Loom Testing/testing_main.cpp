#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

import Engine;
import Scene;
import Server;
import Geometry;

using namespace Loom;


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	Scene scene0{ };
	Scene scene1{ };
	//Scene scene2{ };
	//Scene scene3{ };

	Geometry::Rect* rect = scene0.Attach<Geometry::Rect>(100, 100, 100, 100);

	scene0.Attach<TCP_Server>();

	Engine::Start("C:/Desktop/Loom Project 1/");

	return 0;
};

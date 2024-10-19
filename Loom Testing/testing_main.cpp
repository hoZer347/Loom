#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include "imgui.h"
#include "Macro Helpers.h"

import Mesh;
import Scene;
import Engine;
import Networking;
import GameObject;

import <vector>;
import <thread>;

using namespace Loom;

extern void GL3D_Test();

int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	//Scene scene;
	//Engine::Start("bruh");

	GL3D_Test();

	return 0;
};

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


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	auto thread = std::thread(OpenTCPServerOnThisThread);

	if (thread.joinable())
		thread.join();

	return 0;
};

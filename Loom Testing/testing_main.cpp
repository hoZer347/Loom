#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include "imgui.h"
#include "Macro Helpers.h"

import Mesh;
import Scene;
import Engine;
import GameObject;

import <vector>;

using namespace Loom;


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	Scene scene{ "Scene1" };

	GameObject* gameObject = scene.AddChild("GameObject1");

	gameObject->Attach<Mesh>();

	Engine::Start("C:/Users/3hoze/Desktop/Loom Project 1/");

	return 0;
};

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
import Networking;
import Component;

import <vector>;
import <thread>;

using namespace Loom;


struct Test : Component<Test>
{
	void OnGui() override
	{
		ImGui::Text("Test");
	};
};

int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	//OpenTCPServerOnThisThread("C:/Users/3hoze/Desktop/Vanguards WebGL");
	
	//OpenTCPServerOnThisThread("C:/Users/3hoze/Desktop/Loom Project");

	GameObject::RegisterComponent<Test>();

	Scene scene{ "Gump" };

	Test* test = scene.Attach<Test>();

	Engine::Start("C:/Users/3hoze/Desktop/Loom Test Project 1");

	return 0;
};

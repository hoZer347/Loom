#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include "imgui.h"
#include "Macro Helpers.h"

import Engine;
import Scene;
import Server;
import Geometry;
import Component;
import Serialize;

import <vector>;

using namespace Loom;


struct Test : Component<Test>
{
	int i = 0;
	int j = 5;

	std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

	SERIALIZE(i, j, v);
};

int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	Scene scene{ };

	scene.Attach<Geometry::Rect>(100, 100, 100, 100);

	Test* test0 = scene.Attach<Test>();
	Test* test1 = scene.Attach<Test>();

	test0->i = 423526;
	test0->j = 37746;

	std::cout << test1->i << std::endl;
	std::cout << test1->j << std::endl;

	//Serialize(
	//	"C:/Users/3hoze/Desktop/Loom Project 1/Serialize.txt",
	//	test0,
	//	&test0->i,
	//	&test0->j,
	//	&test0->v);

	//Deserialize(
	//	"C:/Users/3hoze/Desktop/Loom Project 1/Serialize.txt",
	//	test1,
	//	&test1->i,
	//	&test1->j,
	//	&test1->v);

	std::cout << test1->i << std::endl;
	std::cout << test1->j << std::endl;

	for (auto& i : test1->v)
		std::cout << i << std::endl;

	Engine::Start("C:/Users/3hoze/Desktop/Loom Project 1");

	return 0;
};

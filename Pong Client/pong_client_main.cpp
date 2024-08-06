#define LOOM_CLIENT

#include "GL/glew.h"
#include "GLFW/glfw3.h"

#define LOOM_SHOW_MEMORY_OPS

#include "GameObject.h"
#include "Networking.h"
#include "State.h"

#include <thread>
#include <iostream>

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

using namespace Loom;


struct Buh
{
	Buh(int i)
	{ };

	void Render()
	{
		std::cout << "Rendering Buh" << std::endl;
	};

	int i = __COUNTER__;
};


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	if (!glfwInit())
	{
		std::cout << "Test" << std::endl;
	};

	GLFWwindow* window = glfwCreateWindow(1000, 1000, "Test", nullptr, nullptr);

	std::thread(Loom::StartClient).detach();

	Pong::Init();

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glfwSwapBuffers(window);
	};

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
};

#include "GL/glew.h"
#include "GLFW/glfw3.h"

#include "Networking.h"

#include <thread>
#include <iostream>


int main()
{
	//if (!glfwInit())
	//{
	//	std::cout << "Test" << std::endl;
	//};

	//GLFWwindow* window = glfwCreateWindow(1000, 1000, "Test", nullptr, nullptr);

	std::thread(Loom::StartClient).detach();

	//glfwSwapInterval(0);

	//while (!glfwWindowShouldClose(window))
	//{
	//	glfwPollEvents();
	//	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	//	glfwSwapBuffers(window);
	//};

	while (true);

	return 0;
};

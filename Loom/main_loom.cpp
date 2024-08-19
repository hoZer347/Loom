#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>
#include <thread>

#include "imgui.h"

import std;
import Engine;
import DataTypes;
import Log;
import Demos;

using namespace Loom;


int main()
{
	VulkanTriangle_0();

	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(800, 600, "Window", nullptr, nullptr);

	Engine::Init();

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		glfwSwapBuffers(window);
	};

	Engine::Clean();

	glfwDestroyWindow(window);

	glfwTerminate();

	return 0;
};

//#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"

import std;
import Engine;
import DataTypes;
import Log;
import Demos;
import Component;

using namespace Loom;


struct Test0 : Component<Test0> { };
struct Test1 : Component<Test1> { };


int main()
{
	Engine engine{ };
	std::thread t([&]() { engine.Start(); });

	t.join();

	return 0;
};

import Engine;

import Timer;
import Scene;
import Buffer;
import MainMenu;
import GameObject;

import <iostream>;
#include <barrier>

// Borrowed + modified from imgui examples

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Buffer.cuh"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <string>
#include <list>
#include <thread>
#include <glm/glm.hpp>
using namespace glm;

using namespace Loom;

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif
#include <queue>

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
};

namespace Loom
{
	void Engine::DoTasks() noexcept
	{
		std::lock_guard lock{ mutex };

		if (!taskQueue.empty())
		{
			while (!taskQueue.empty())
			{
				taskQueue.front()();
				taskQueue.pop();
			};
		};
	};

	void Engine::QueueTask(const Task& task)
	{
		std::lock_guard lock{ mutex };
		taskQueue.push(task);
	};

	void Engine::Start()
	{
		glfwSetErrorCallback(glfw_error_callback);
		
		glfwInit();

		// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100
		const char* glsl_version = "#version 100";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
		const char* glsl_version = "#version 150";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

		// Create window with graphics context
		window = glfwCreateWindow(1280, 720, "Loom", nullptr, nullptr);
		glfwMakeContextCurrent(window);
		glfwSwapInterval(0);
		glewInit();

		glClearColor(0.5f, 0.5f, 0.5f, 0.0f);

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glEnable(GL_DEPTH);

		glfwSetWindowSizeCallback(window,
			[](GLFWwindow* window, int w, int h)
			{
				glViewport(0, 0, w, h);
			});

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
		io.ConfigViewportsNoAutoMerge = true;
		io.ConfigViewportsNoTaskBarIcon = true;

		// Setup Dear ImGui style
		//ImGui::StyleColorsDark();
		ImGui::StyleColorsLight();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		};

		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
		ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
		ImGui_ImplOpenGL3_Init(glsl_version);

		// Our state
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		DoTasks();

		Scene::is_engine_running = true;

		std::barrier barrier
		{
			std::thread::hardware_concurrency() + 2,
			[&]() noexcept
			{
				DoTasks();
				glfwSwapBuffers(window);
			}
		};

		isRunning = true;

		std::vector<std::thread> threads;
		for (unsigned int i = 0; i < std::thread::hardware_concurrency(); i++)
			threads.emplace_back(
				[&, i]()
				{
					while (isRunning)
					{
						barrier.arrive_and_wait();
						for (auto& scene : Scene::allScenes)
							if (scene->thread_id == i)
								scene->root.Update(i);
					};

					barrier.arrive_and_drop();
				});

		threads.emplace_back(
			[&]()
			{
				while (isRunning)
				{
					barrier.arrive_and_wait();
					for (auto& scene : Scene::allScenes)
						scene->root.Render();
				};

				barrier.arrive_and_drop();
			});

		// Main loop
#ifdef __EMSCRIPTEN__
	// For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
	// You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
		io.IniFilename = nullptr;
		EMSCRIPTEN_MAINLOOP_BEGIN
#else
		while (!glfwWindowShouldClose(window))
#endif
		{
			glfwPollEvents();

			// Start the Dear ImGui frame
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			//


			// GUI
			MainMenu::Gui();

			if (doGUI)
				for (auto& scene : Scene::allScenes)
				{
					if (ImGui::Begin(scene->name))
						scene->root.Gui();
					ImGui::End();
				};
			//


			// Rendering
			ImGui::Render();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			//


			// Update and Render additional Platform Windows
			// (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
			//  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				GLFWwindow* backup_current_context = glfwGetCurrentContext();
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
				glfwMakeContextCurrent(backup_current_context);
			};

			for (auto& scene : Scene::allScenes)
				scene->root.Render();

			barrier.arrive_and_wait();
		};
#ifdef __EMSCRIPTEN__
		EMSCRIPTEN_MAINLOOP_END;
#endif
		barrier.arrive_and_drop();

		isRunning = false;

		for (auto& thread : threads)
			thread.join();

		DoTasks();

		// Cleanup
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glfwDestroyWindow(window);
		glfwTerminate();
		//
	};

	void Engine::Stop()
	{
		glfwSetWindowShouldClose(window, true);
	};
};

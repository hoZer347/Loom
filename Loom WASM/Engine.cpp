#include "Engine.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif

#include "OpenGL.h"
#include "Scene.h"
#include "Input.h"
#include "Shaders.h"
#include "State.h"

#include <iostream>
#include <atomic>


namespace Loom
{
	// settings
	const unsigned int SCR_WIDTH = 800;
	const unsigned int SCR_HEIGHT = 600;

	const char* glsl_version;

	ImGuiIO* io;

	void Engine::InitImGui()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		io = &ImGui::GetIO(); (void)io;
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
		io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
		io->ConfigViewportsNoAutoMerge = true;
		//io.ConfigViewportsNoTaskBarIcon = true;

		// Setup Dear ImGui style
		//ImGui::StyleColorsDark();
		ImGui::StyleColorsLight();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		};

		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForOpenGL(Engine::window, true);
#ifdef __EMSCRIPTEN__
		ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
		ImGui_ImplOpenGL3_Init(glsl_version);
	};

	void Engine::RenderImGui()
	{
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		//

		// GUI
		//MainMenu::Gui();

		if (Engine::doGUI)
			for (auto& scene : Scene::allScenes)
			{
				if (ImGui::Begin(scene->NameAndID().c_str()))
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
		if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		};
	};

	void resizeCanvas()
	{
		int width, height;

#if __EMSCRIPTEN__
		// Get the current size of the canvas
		emscripten_get_canvas_element_size("#canvas", &width, &height);

		// Update GLFW's window size
		glfwSetWindowSize(
			Engine::window,
			width,
			height);
#else
		glfwGetWindowSize(
			Engine::window,
			&width,
			&height);
#endif
		Input::screen_width = width;
		Input::screen_height = height;

		// Explicitly update the OpenGL viewport
		glViewport(0, 0, width, height);
	};

	void Engine::Start()
	{
#if __EMSCRIPTEN__
		emscripten_set_main_loop(renderFrame, 0, true);
#else
		while (!glfwWindowShouldClose(window))
		{
			State::Update();
			renderFrame();
		};
#endif

		glDeleteBuffers(1, &VBO);
		glDeleteBuffers(1, &EBO);

		glfwTerminate();

		isRunning = false;
	};

	void Engine::Stop()
	{
		isRunning = false;
		glfwSetWindowShouldClose(window, true);
	}

	void Engine::renderFrame()
	{
		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		while (taskQueue.size())
		{
			std::scoped_lock lock(mutex);
			Task task = taskQueue.front();
			task();
			taskQueue.pop();
		};

		onUpdate();

		//Shader::SetUniforms();

		RenderImGui();

		for (auto& scene : Scene::allScenes)
			scene->root.Render();

		glFlush();

		glfwSwapBuffers(window);
		glfwPollEvents();
	};

	void Engine::SetUpdateFunction(const Task& task)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		onUpdate = task;
	};

	void Engine::QueueTask(const Task& task)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		taskQueue.push(task);
	};

	const size_t Engine::GetUniqueID()
	{
		static std::atomic<size_t> id = 0;
		return id++;
	};

	Engine::Engine()
	{
		if (!glfwInit())
			std::cerr << "GLFW failed to init" << std::endl;

		// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100
		glsl_version = "#version 100";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
		glsl_version = "#version 150";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
		glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

		window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "", NULL, NULL);
		if (window == NULL)
		{
			std::cout << "Failed to create GLFW window" << std::endl;
			glfwTerminate();
		};

		glfwMakeContextCurrent(window);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

#ifndef __EMSCRIPTEN__
		glewExperimental = true;

		if (glewInit())
			std::cerr << "Glew failed to init" << std::endl;
#endif

		Input::Init();

		// Setting the Resize Callback
#if __EMSCRIPTEN__
		emscripten_set_resize_callback(
			EMSCRIPTEN_EVENT_TARGET_WINDOW,
			nullptr,
			EM_TRUE,
			[](int, const EmscriptenUiEvent*, void*) -> EM_BOOL
			{
				std::cout << "Resize Callback" << std::endl;
				resizeCanvas();
				return EM_TRUE;
			});
#else
		glfwSetFramebufferSizeCallback(
			window,
			[](GLFWwindow*, int w, int h)
			{
				glViewport(0, 0, w, h);
			});
#endif
		//

		glEnable(GL_DEPTH_TEST);
		//glEnable(GL_MULTISAMPLE);
		glfwWindowHint(GLFW_SAMPLES, 4);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


#ifndef __EMSCRIPTEN__
#ifndef NDEBUG
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(
			[](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
			{
				std::cerr << "OpenGL Debug: " << message << std::endl;
			},
			nullptr);
#endif
#endif

		if (!glfwGetCurrentContext())
		{
			std::cerr << "No OpenGL context is currently active!" << std::endl;
			return;
		};

		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
		{
			std::cerr << "OpenGL error: " << err << std::endl;
			return;
		};

		std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
		std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

		glGenBuffers(1, &VBO);
		glGenBuffers(1, &EBO);

#ifndef __EMSCRIPTEN__
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(Engine::VAO);
#endif

		InitImGui();
	};

	Engine::~Engine()
	{
		Stop();
	};
};

#include "Engine.h"

#include "OpenGL.h"
#include "Scene.h"
#include "Input.h"

#include <iostream>
#include <atomic>


namespace Loom
{
	// settings
	const unsigned int SCR_WIDTH = 800;
	const unsigned int SCR_HEIGHT = 600;

	void resizeCanvas()
	{
#if __EMSCRIPTEN__
		int width, height;
		// Get the current size of the canvas
		emscripten_get_canvas_element_size("#canvas", &width, &height);

		// Update GLFW's window size
		glfwSetWindowSize(Engine::window, width, height);

		// Explicitly update the OpenGL viewport
		glViewport(0, 0, width, height);
#endif
	};


	void Engine::Init()
	{
		if (!glfwInit())
			std::cerr << "GLFW failed to init" << std::endl;

#ifdef __EMSCRIPTEN__
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
		window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
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
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(
			[](GLenum source, GLenum type, GLuint m_id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
			{
				std::cerr << "OpenGL Debug: " << message << std::endl;
			},
			nullptr);
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

		resizeCanvas();
	};

	void Engine::Start()
	{
#if __EMSCRIPTEN__
		emscripten_set_main_loop(renderFrame, 0, true);
#else
		while (!glfwWindowShouldClose(window))
			renderFrame();
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
		resizeCanvas();

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
		static std::atomic<size_t> m_id = 0;
		return m_id++;
	};

	Engine::Engine()
	{
		// TODO: Tie program lifecycle to engine lifecycle
	};

	Engine::~Engine()
	{
		// TODO: Tie program lifecycle to engine lifecycle
	};
};

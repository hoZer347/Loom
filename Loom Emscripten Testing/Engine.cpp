#include "Engine.h"

#include "OpenGL.h"

#include <GLFW/glfw3.h>

#include <iostream>


namespace Loom
{
	static inline GLFWwindow* window = nullptr;

	// settings
	const unsigned int SCR_WIDTH = 800;
	const unsigned int SCR_HEIGHT = 600;

	//static inline GLFWwindow* window = nullptr;
	unsigned int shaderProgram, VBO, EBO;

	const char* vertexShaderSource =
#if __EMSCRIPTEN__
		"#version 300 es\n"
#else
		"#version 330 core\n"
#endif
		"layout (location = 0) in vec3 aPos;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(aPos, 1.0);\n"
		"}\n";

	const char* fragmentShaderSource =
#if __EMSCRIPTEN__
		"#version 300 es\n"
#else
		"#version 330 core\n"
#endif
		"precision mediump float;\n"
		"out vec4 FragColor;\n"
		"void main()\n"
		"{\n"
		"   FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n"
		"}\n";

	void framebuffer_size_callback(GLFWwindow* window, int width, int height);
	void processInput(GLFWwindow* window);
	void renderFrame();
	void resizeCanvas();

	void processInput(GLFWwindow* window)
	{
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
#if __EMSCRIPTEN__
			emscripten_cancel_main_loop();
#endif
			glfwSetWindowShouldClose(window, true);
		};
	};

	void framebuffer_size_callback(GLFWwindow* window, int width, int height)
	{
		glViewport(0, 0, width, height);
	};

	void resizeCanvas()
	{
#if __EMSCRIPTEN__
		int width, height;
		emscripten_get_canvas_element_size("#canvas", &width, &height);
		glfwSetWindowSize(window, width, height);
		framebuffer_size_callback(window, width, height);
#endif
	};

	void renderFrame()
	{
		processInput(window);

		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(shaderProgram);

		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	};

	Engine::Engine()
	{
		if (!glfwInit())
			std::cerr << "GLFW failed to init" << std::endl;

#ifdef __EMSCRIPTEN__
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
		//glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		//glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		//glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

		glewExperimental = true;
#endif
		window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
		if (window == NULL)
		{
			std::cout << "Failed to create GLFW window" << std::endl;
			glfwTerminate();
		};

		glfwMakeContextCurrent(window);
		glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

#ifndef __EMSCRIPTEN__
		if (!glewInit())
			std::cerr << "Glew failed to init" << std::endl;
#endif

		glfwSetFramebufferSizeCallback(
			window,
			[](GLFWwindow*, int w, int h)
			{
				glViewport(0, 0, w, h);
			});

#ifndef __EMSCRIPTEN__
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(
			[](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
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
		}

		std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
		std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

		int w, h;
		glfwGetWindowSize(window, &w, &h);
		framebuffer_size_callback(window, w, h);

		unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
		glCompileShader(vertexShader);

		int success;
		char infoLog[512];
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
		};

		unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
		glCompileShader(fragmentShader);
		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
		};

		shaderProgram = glCreateProgram();
		glAttachShader(shaderProgram, vertexShader);
		glAttachShader(shaderProgram, fragmentShader);
		glLinkProgram(shaderProgram);
		glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
		if (!success)
		{
			glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		};

		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		float vertices[] =
		{
			 0.5f,  0.5f, 0.0f,
			 0.5f, -0.5f, 0.0f,
			-0.5f, -0.5f, 0.0f,
			-0.5f,  0.5f, 0.0f
		};

		unsigned int indices[] =
		{
			0, 1, 3,
			1, 2, 3
		};

		glGenBuffers(1, &VBO);
		glGenBuffers(1, &EBO);

		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

		resizeCanvas();

#if __EMSCRIPTEN__
		emscripten_set_resize_callback(
			EMSCRIPTEN_EVENT_TARGET_WINDOW,
			nullptr,
			EM_TRUE,
			[](int, const EmscriptenUiEvent*, void*) -> EM_BOOL
			{
				resizeCanvas();
				return EM_TRUE;
			});

		emscripten_set_main_loop(renderFrame, 0, true);
#else
		while (!glfwWindowShouldClose(window))
			renderFrame();
#endif

		glDeleteBuffers(1, &VBO);
		glDeleteBuffers(1, &EBO);

		glfwTerminate();
	};

	Engine::~Engine()
	{

	};
};

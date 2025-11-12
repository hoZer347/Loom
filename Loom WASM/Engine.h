#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <iostream>

struct GLFWwindow;


namespace Loom
{
	typedef std::function<void()> Task;
	struct Scene;

	/**
	* Loom::Engine
	* - Manages the runtime of the application
	* - Updates all GameObjects
	* - Manages the backend OpenGL and ImGui loops
	* - Should decouple OpenGL and ImGui, but currently does not
	*/
	struct Engine final
	{
		Engine();
		~Engine();

		void Start();
		void Stop();

		static void SetUpdateFunction(const Task& task);

		static void QueueTask(const Task& task);

		static inline bool doGUI = true;

		static inline unsigned int shaderProgram = 0, VAO = 0, VBO = 0, EBO = 0;

		static const size_t GetUniqueID();

		static inline GLFWwindow* window = nullptr;

		static void renderFrame();

		static inline bool isRunning;

	private:
		static void InitImGui();
		static void RenderImGui();
		static void DoTasks() noexcept;

		static inline const char* projectDirectory;

		static inline Task onUpdate = []() { };

		static inline std::recursive_mutex mutex;
		static inline std::queue<Task> taskQueue;
	};
};

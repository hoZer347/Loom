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
	struct Buffer;
	struct Scene;

	// TODO: Make this not static
	struct Engine final
	{
		Engine();
		~Engine();

		void Init();
		void Start();
		void Stop();

		static void SetUpdateFunction(const Task& task);

		static void QueueTask(const Task& task);

		static inline bool doGUI = true;
		static inline bool isRunning;

		static inline unsigned int shaderProgram = 0, VAO = 0, VBO = 0, EBO = 0;

		static const size_t GetUniqueID();

		static inline GLFWwindow* window = nullptr;

		static void renderFrame();

	private:
		static void DoTasks() noexcept;

		static inline const char* projectDirectory;

		static inline Task onUpdate = []() { };

		static inline std::recursive_mutex mutex;
		static inline std::queue<Task> taskQueue;
	};
};

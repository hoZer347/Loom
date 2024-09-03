export module Engine;

import <GLFW/glfw3.h>;
import <functional>;
import <mutex>;
import <queue>;


namespace Loom
{
	export typedef std::function<void()> Task;
	export struct Buffer;
	export struct Scene;

	export struct Engine final
	{
		void Start();
		void Stop();

		void QueueTask(const Task& task);

		bool doGUI = true;
		bool isRunning;

	private:
		void DoTasks() noexcept;

		
		std::recursive_mutex mutex;
		std::queue<Task> taskQueue;
		GLFWwindow* window = nullptr;
	};
};

export module Engine;

import <GLFW/glfw3.h>;
import <functional>;
import <mutex>;
import <queue>;
import <string>;
import <iostream>;


namespace Loom
{
	export typedef std::function<void()> Task;
	export struct Buffer;
	export struct Scene;

	// TODO: Make this not static
	export struct Engine final
	{
		static void Start(const std::string& projectDirectory);
		static void Stop();

		static void QueueTask(const Task& task);

		static inline bool doGUI = true;
		static inline bool isRunning;

		static const size_t GetUniqueID();

		static inline GLuint m_VAO = 0;
		static inline GLuint m_VTXS = 0;
		static inline GLuint m_INDS = 0;

		Engine() = delete;

	private:
		static void DoTasks() noexcept;
		
		static inline const char* projectDirectory;

		static inline std::recursive_mutex mutex;
		static inline std::queue<Task> taskQueue;
	};
};

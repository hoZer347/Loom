export module Engine;

import Buffer;

import <GLFW/glfw3.h>;
import <atomic>;


namespace Loom
{
	export struct Engine final
	{
		void Start();
		void Stop();

		static inline unsigned char* CPU_buffer = nullptr;
		static inline unsigned char* GPU_buffer = nullptr;
		static inline unsigned int* GPU_dims = nullptr;

		bool doGUI = true;

	private:
		GLFWwindow* window = nullptr;
	};
};

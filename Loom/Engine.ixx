export module Engine;

import DataTypes;

import <GLFW/glfw3.h>;
import <functional>;
import <barrier>;
import <memory>;
import <atomic>;


namespace Loom
{
	export struct Engine final
	{
		void Start();
		void Stop();

		constexpr inline void Add(std::function<void()>& func) noexcept { funcs.push_back(func); };
		
		void* buffer = nullptr;

		static inline std::atomic<size_t> id_counter = 0;

	private:
		GLFWwindow* window = nullptr;
		std::vector<std::function<void()>> funcs{ };
	};
};

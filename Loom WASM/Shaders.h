#pragma once

#include <mutex>
#include <string>
#include <unordered_map>


namespace Loom
{
	struct Shader final
	{
		Shader();
		Shader(const std::string& file_path);

		~Shader();

		const std::string file_path;
		const uint32_t id; // TODO: Make this const

	private:
		uint32_t CompileSource(const std::string& file_path);

#ifdef __EMSCRIPTEN__
		void Request(
			const std::string& file_path,
			std::string& shader_source);
#endif

		static inline std::mutex mutex;

	protected:
	public:
		friend struct Engine;

		static inline std::unordered_map<std::string, uint32_t> shaders;
	};
};

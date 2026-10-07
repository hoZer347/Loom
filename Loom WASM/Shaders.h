#pragma once

#include "Loom API.h"

#include <istream>
#include <mutex>
#include <string>
#include <unordered_map>


namespace Loom
{
	struct LOOM_API Shader final
	{
		Shader();
		Shader(const std::string& file_path);

		// Compiles source the caller already has, for a shader the engine
		// carries itself rather than reading off disk. The name keys it in the
		// same program cache a file path would.
		Shader(const std::string& name, std::istream& source);

		~Shader();

		const std::string file_path;
		const uint32_t id; // TODO: Make this const

	private:
		uint32_t CompileSource(const std::string& file_path);
		uint32_t CompileStream(const std::string& name, std::istream& source);

		// Splits the source into its stages and links them. The caller holds
		// the mutex.
		uint32_t Build(const std::string& name, std::istream& source);

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

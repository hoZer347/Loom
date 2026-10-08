#pragma once

#include "Loom API.h"

#include "ShaderVariables.h"

#include <filesystem>
#include <istream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>


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

		static constexpr const char* extension = ".shader";

		// Compiles the file again, for after it has been edited, and swaps the
		// new program in. A file that no longer compiles keeps the old program
		// and says why.
		bool Reload();

		// Reloads when the file has been written since it was last compiled.
		void ReloadIfChanged();

		const std::string file_path;

		// The uniforms the source declares for its users to fill in. Declared
		// before id, which is what fills them in.
		std::vector<ShaderVariable> variables;

	private:
		std::filesystem::file_time_type m_written;

	public:
		uint32_t id;

	private:
		uint32_t CompileSource(const std::string& file_path);
		uint32_t CompileStream(const std::string& name, std::istream& source);

		// Splits the source into its stages and links them. The caller holds
		// the mutex.
		uint32_t Build(const std::string& name, std::istream& source);

		// Throws std::runtime_error when there is nothing to read.
		std::string ReadSource(const std::string& file_path);

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

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

		// What a shader file compiles to: its stages with the engine's
		// sections added as its settings ask, and the shadow pass's stages,
		// empty unless shadows are on.
		struct Stages
		{
			ShaderSettings settings;
			std::string vertex;
			std::string fragment;
			std::string shadowVertex;
			std::string shadowFragment;
		};

		// Throws std::runtime_error at a setting it does not know, or a stage
		// the sections it asks for have nothing to add to.
		static Stages Assemble(const std::string& source);

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
		ShaderSettings settings;

		// The program the light draws the shadow map with, or 0 when shadows
		// are off. Declared before id, which is what fills it in.
		uint32_t shadow_id = 0;

	private:
		std::filesystem::file_time_type m_written;

	public:
		uint32_t id;

	private:
		uint32_t CompileSource(const std::string& file_path);
		uint32_t CompileStream(const std::string& name, std::istream& source);

		// Assembles the source and links its programs. The caller holds the
		// mutex.
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

		struct Programs
		{
			uint32_t id = 0;
			uint32_t shadow = 0;
		};

		static inline std::unordered_map<std::string, Programs> shaders;
	};
};

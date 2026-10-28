#pragma once

#include "Loom API.h"

#include "ShaderVariables.h"

#include <filesystem>
#include <istream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	// Something compiling a shader file had to say about it.
	struct ShaderMessage
	{
		// The file's line, from 1, or 0 for a message about no line of it (the
		// link, or the engine's own GLSL).
		size_t line = 0;
		bool error = true;
		std::string text;
	};

	struct LOOM_API Shader final
	{
		#pragma region Construction

		Shader();
		Shader(const std::string& file_path);

		// Compiles source the caller already has, for a shader the engine
		// carries itself rather than reading off disk. The name keys it in the
		// same program cache a file path would.
		Shader(const std::string& name, std::istream& source);

		~Shader();

		#pragma endregion

		#pragma region Assembly

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

		// Throws std::runtime_error at a setting it does not know, a stage the
		// sections it asks for have nothing to add to, or a PIXEL PASS
		// section. preVertex is a PreVertex's code, run in front of the vertex
		// stage and the shadow pass's.
		static Stages Assemble(const std::string& source, const std::string& preVertex = { });

		// What a pixel shader file compiles to: one quad over the view, and the
		// file as its fragment stage behind the pixel pass library.
		struct PixelStages
		{
			std::string vertex;
			std::string fragment;
		};

		// Throws std::runtime_error at a kernel with no centre, or a file that
		// writes no colour.
		static PixelStages AssemblePixel(const std::string& source);

		#pragma endregion

		#pragma region Loading

		static constexpr char extension[] = ".shader";

		// A pixel shader's file, which compiles to its pixel pass alone, as id.
		static constexpr char pixel_extension[] = ".loompixel";

		// Compiles the file again, for after it has been edited, and swaps the
		// new program in. A file that no longer compiles keeps the old program
		// and says why.
		bool Reload();

		// Reloads when the file has been written since it was last compiled.
		void ReloadIfChanged();

		// Read in place of the file at file_path from now on, so a page can
		// edit a shader it has no file for.
		static void SetSource(const std::string& file_path, const std::string& source);

		// What SetSource last gave file_path, if anything.
		static std::optional<std::string> SourceFor(const std::string& file_path);

		// Why the last compile that failed did.
		static inline std::string lastError;

		#pragma endregion

		#pragma region Queries

		static bool IsPixel(const std::string& file_path);

		#pragma endregion

#ifndef __EMSCRIPTEN__
		#pragma region Checking

		// Compiles the text of a shader file the way the Vulkan renderer would,
		// whichever renderer is running, without making a program of it.
		static std::vector<ShaderMessage> Check(const std::string& text);

		// The same for the text of a pixel shader file.
		static std::vector<ShaderMessage> CheckPixel(const std::string& text);

		// Compiles a PreVertex's code in front of the engine's own vertex
		// stage, with what is wrong on the code's own lines.
		static std::vector<ShaderMessage> CheckPreVertex(const std::string& text);

		#pragma endregion
#endif

		#pragma region Fields

		const std::string file_path;

		// The uniforms the source declares for its users to fill in. Declared
		// before id, which is what fills them in.
		std::vector<ShaderVariable> variables;
		ShaderSettings settings;

		// The program the light draws the shadow map with, or 0 when shadows
		// are off. Declared before id, which is what fills it in.
		uint32_t shadow_id = 0;

		// The program and the shadow program drawing many meshes at once, each
		// with a model matrix and colour of its own, or 0 for stages that
		// declare either some other way. Declared before id, which is what
		// fills them in.
		uint32_t instanced_id = 0;
		uint32_t shadow_instanced_id = 0;

		// The program and the shadow program drawing a mesh a Skin bends, its
		// vertices bent on the GPU by the joints, or 0 for a vertex stage with
		// no main, that does not read aPos as a vec3, or whose UV layers leave
		// no room for the Skin's. Declared before id, which is what fills them in.
		uint32_t skinned_id = 0;
		uint32_t shadow_skinned_id = 0;

		// The vertex stage the program was linked from, for drawing the same
		// shape with another fragment stage. Declared before id, which is what
		// fills it in.
		std::string vertex;

		// The vertex stage skinned_id was linked from, or empty without one.
		// Declared before id, which is what fills it in.
		std::string skinned_vertex;

		#pragma endregion

	private:
		std::filesystem::file_time_type m_written;

	public:
		uint32_t id;

	private:
		#pragma region Loading

		struct Programs
		{
			uint32_t id = 0;
			uint32_t shadow = 0;
			uint32_t instanced = 0;
			uint32_t shadowInstanced = 0;
			uint32_t skinned = 0;
			uint32_t shadowSkinned = 0;
			ShaderSettings settings;
			std::string vertex;
			std::string skinnedVertex;
		};

		uint32_t CompileSource(const std::string& file_path);
		uint32_t CompileStream(const std::string& name, std::istream& source);

		// Takes on the settings, other programs and vertex stage of programs,
		// and answers its id.
		uint32_t Cached(const Programs& programs);

		// Throws std::runtime_error when there is nothing to read.
		std::string ReadSource(const std::string& file_path);

		#pragma endregion

		#pragma region Assembly

		// Assembles the source, with preVertex run on its vertices, and links
		// its programs. The caller holds the mutex.
		uint32_t Build(const std::string& name, std::istream& source, const std::string& preVertex);

		#pragma endregion

#ifdef __EMSCRIPTEN__
		#pragma region Fetching

		static void Request(
			const std::string& file_path,
			std::string& shader_source);

		#pragma endregion
#endif

		static inline std::mutex mutex;

		// SetSource's, by normalised path.
		static inline std::mutex sourcesMutex;
		static inline std::unordered_map<std::string, std::string> sources;

	protected:
	public:
		friend struct Engine;
		friend struct PreVertex;

		static inline std::unordered_map<std::string, Programs> shaders;
	};
};

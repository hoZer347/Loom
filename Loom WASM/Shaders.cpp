#include "Shaders.h"

#include "GlslCompiler.h"
#include "Light.h"
#include "Renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include <algorithm>
#include <sstream>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <optional>
#include <regex>
#include <stdexcept>

#define SHADER_PATH ""


namespace Loom
{
	static inline std::string defaultShader;

	namespace
	{
		// The file is source string 0 to the compiler and the engine's GLSL is
		// 1, so nothing said about the engine's is taken for the file's.
		constexpr int FILE_SOURCE = 0;
		constexpr int LIBRARY_SOURCE = 1;

		// From GLSL 3.30 and ES 3.00 on, #line numbers the line after it.
		std::string LineDirective(size_t line, int source)
		{
			return "#line " + std::to_string(line) + ' ' + std::to_string(source) + '\n';
		};

		// Code the engine puts in a stage, numbered apart from the file's.
		std::string EngineCode(const std::string& code)
		{
			return code.empty() ? code : LineDirective(1, LIBRARY_SOURCE) + code;
		};

		// The vertex stage of a file without one: the mesh's positions, through
		// its transform and the camera.
		constexpr const char* DEFAULT_VERTEX = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		// The depth the vertex stage puts out is all the shadow map needs.
		constexpr const char* SHADOW_FRAGMENT = R"(
void main()
{
}
)";

		// A transparent shader's shadow pass keeps its alpha's share of each
		// 4x4 block of texels. The shadow map's filtering spans a block, so it
		// averages them back into a shadow as dark as the alpha.
		constexpr const char* DITHER_LIBRARY = R"(
const int LOOM_DITHER_SIZE = 4;
const float LOOM_DITHER_LEVELS = 16.0;
const float LOOM_DITHER_CENTRE = 0.5;
const float LOOM_DITHER[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

bool LoomDitheredOut(float alpha)
{
	ivec2 cell = ivec2(gl_FragCoord.xy) % LOOM_DITHER_SIZE;

	return alpha * LOOM_DITHER_LEVELS < LOOM_DITHER[cell.y * LOOM_DITHER_SIZE + cell.x] + LOOM_DITHER_CENTRE;
}
)";

		constexpr const char* SHADOW_PROGRAM_SUFFIX = " shadows";
		constexpr const char* INSTANCED_PROGRAM_SUFFIX = " instanced";

		// What the shading section's vertex half reads, and the declaration it
		// adds when the stage has not made its own.
		struct ShadingInput
		{
			const char* name;
			const char* declaration;
		};

		constexpr ShadingInput SHADING_INPUTS[] =
		{
			{ "aPos", "layout(location = 0) in vec3 aPos;" },
			{ "aNormal", "layout(location = 2) in vec3 aNormal;" },
			{ "u_model", "uniform mat4 u_model;" },
		};

		const std::regex& MainPattern()
		{
			// The whitespace is kept, so a main split over lines keeps the
			// lines after it where #line numbered them.
			static const std::regex pattern(R"(\b(void\s+)main(\s*\(\s*(?:void\s*)?\)))");

			return pattern;
		};

		// The fragment stage's colour, by name.
		const std::regex& ColorPattern()
		{
			static const std::regex pattern(R"(\bout\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+(\w+)\s*;)");

			return pattern;
		};

		bool Declares(const std::string& stage, const std::string& name)
		{
			const std::regex declaration(R"(\b(?:in|uniform)\s+(?:(?:lowp|mediump|highp)\s+)?\w+\s+)" + name + R"(\s*;)");

			return std::regex_search(stage, declaration);
		};

		// The stage's own main, renamed for the engine's to call before adding
		// its sections.
		std::string WithMainRenamed(const std::string& stage, const std::string& which)
		{
			if (!std::regex_search(stage, MainPattern()))
				throw std::runtime_error("Shading needs the " + which + " stage to have a main()");

			return std::regex_replace(stage, MainPattern(), "$1loom_main$2");
		};

		std::string ShadedVertex(const std::string& stage, bool shadows)
		{
			std::string shaded = WithMainRenamed(stage, "vertex") + '\n' + LineDirective(1, LIBRARY_SOURCE);

			for (const ShadingInput& input : SHADING_INPUTS)
				if (!Declares(stage, input.name))
					shaded += std::string(input.declaration) + '\n';

			shaded += "out vec3 loom_World;\nout vec3 loom_Normal;\n";

			if (shadows)
				shaded += "out vec4 loom_LightSpace;\n";

			// The model's inverse transpose keeps a normal square to its face
			// however the model is stretched. A mesh without normals gives
			// zeros, which stay zeros.
			shaded +=
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tvec4 loom_world = u_model * vec4(aPos, 1.0);\n"
				"\tloom_World = loom_world.xyz;\n"
				"\tloom_Normal = transpose(inverse(mat3(u_model))) * aNormal;\n";

			if (shadows)
				shaded += "\tloom_LightSpace = LoomLightSpace(loom_world, loom_Normal);\n";

			return shaded + "}\n";
		};

		std::string ShadedFragment(const std::string& stage, bool shadows)
		{
			std::smatch color;

			if (!std::regex_search(stage, color, ColorPattern()))
				throw std::runtime_error("Shading needs the fragment stage to write an out vec4 colour");

			std::string shaded = WithMainRenamed(stage, "fragment") + '\n' + LineDirective(1, LIBRARY_SOURCE) + "in vec3 loom_World;\nin vec3 loom_Normal;\n";

			if (shadows)
				shaded += "in vec4 loom_LightSpace;\n";

			// The face's own normal comes from the surface's slope across the
			// screen, and always faces the camera. A mesh with normals is lit by
			// them instead, smoothly across its faces, turned to the camera's
			// side so a sheet seen from behind is lit as its back. One without
			// them gives zeros, and is lit flat face by face.
			return shaded +
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tvec3 loom_face = normalize(cross(dFdx(loom_World), dFdy(loom_World)));\n"
				"\tvec3 loom_normal = dot(loom_Normal, loom_Normal) > 0.0 ? normalize(loom_Normal) : loom_face;\n"
				"\tloom_normal = dot(loom_normal, loom_face) < 0.0 ? -loom_normal : loom_normal;\n\n"
				"\t" + color[1].str() + ".rgb *= LoomLight(loom_normal" + (shadows ? ", loom_LightSpace" : "") + ");\n"
				"}\n";
		};

		std::string TransparentShadowFragment(const std::string& stage)
		{
			std::smatch color;

			if (!std::regex_search(stage, color, ColorPattern()))
				throw std::runtime_error("Transparency with shadows needs the fragment stage to write an out vec4 colour");

			// The pass has no colour attachment, so the colour becomes a plain
			// variable, losing any layout qualifier with its out.
			static const std::regex output(R"((?:\blayout\s*\([^)]*\)\s*)?\bout\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+(\w+)\s*;)");

			const std::string withoutOutput = std::regex_replace(WithMainRenamed(stage, "fragment"), output, "vec4 $1;");

			return withoutOutput + '\n' + EngineCode(DITHER_LIBRARY) +
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tif (LoomDitheredOut(" + color[1].str() + ".a))\n"
				"\t\tdiscard;\n"
				"}\n";
		};

		// The stages of a program drawn as instances.
		struct InstancedStages
		{
			std::string vertex;
			std::string fragment;
		};

		// The stages, reading the model matrix and colour each draw would set
		// from each instance, or none when the vertex stage declares no model
		// matrix, either is declared some other way, or the fragment stage
		// reads the model matrix.
		std::optional<InstancedStages> Instanced(const std::string& vertex, const std::string& fragment)
		{
			static const std::regex model(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?mat4\s+u_model\s*;)");
			static const std::regex color(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?vec3\s+u_color\s*;)");
			static const std::regex otherwise(R"(\buniform\b[^;]*\bu_(?:model|color)\b)");
			static const std::regex readsModel(R"(\bu_model\b)");

			if (!std::regex_search(vertex, model))
				return std::nullopt;

			InstancedStages stages{ std::regex_replace(std::regex_replace(vertex, model, ""), color, ""), std::regex_replace(fragment, color, "") };

			if (std::regex_search(stages.vertex, otherwise) ||
				std::regex_search(stages.fragment, otherwise) ||
				std::regex_search(fragment, readsModel))
				return std::nullopt;

			// The colour is the instance's, handed on to the fragment stage
			// unblended.
			stages.vertex =
				"layout(location = " + std::to_string(Renderer::InstanceModelLocation) + ") in mat4 loom_InstanceModel;\n"
				"layout(location = " + std::to_string(Renderer::InstanceColorLocation) + ") in vec3 loom_InstanceColor;\n"
				"flat out vec3 loom_Color;\n"
				"#define u_model loom_InstanceModel\n"
				"#define u_color loom_InstanceColor\n" +
				std::regex_replace(stages.vertex, MainPattern(), "$1loom_instance_main$2") +
				"\nvoid main()\n{\n"
				"\tloom_Color = loom_InstanceColor;\n"
				"\tloom_instance_main();\n"
				"}\n";

			stages.fragment = "flat in vec3 loom_Color;\n#define u_color loom_Color\n" + stages.fragment;

			return stages;
		};

		// The program, or 0 when the stages do not compile.
		uint32_t CreateIfCompiles(Renderer& renderer, const std::string& name, const std::optional<InstancedStages>& stages)
		{
			if (!stages)
				return 0;

			try
			{
				return renderer.CreateProgram(name, stages->vertex, stages->fragment);
			}
			catch (const std::runtime_error&)
			{
				return 0;
			};
		};

#ifndef __EMSCRIPTEN__
		// glslang's "ERROR: 0:12: 'x' : undeclared identifier", and its warnings.
		const std::regex& MessagePattern()
		{
			static const std::regex pattern(R"(^(ERROR|WARNING): (.*)$)");
			return pattern;
		};

		const std::regex& PlacePattern()
		{
			static const std::regex pattern(R"(^(\d+):(\d+): (.*)$)");
			return pattern;
		};

		// What glslang ends a stage that failed with.
		const std::regex& EndPattern()
		{
			static const std::regex pattern(R"(^\d+ compilation errors|: compilation terminated)");
			return pattern;
		};

		std::vector<ShaderMessage> ParseMessages(const std::string& log)
		{
			std::vector<ShaderMessage> messages;
			std::istringstream lines(log);

			for (std::string line; std::getline(lines, line);)
			{
				std::smatch match;

				if (!std::regex_match(line, match, MessagePattern()))
					continue;

				ShaderMessage message;
				message.error = match[1] == "ERROR";
				message.text = match[2];

				if (std::regex_search(message.text, EndPattern()))
					continue;

				std::smatch place;

				if (std::regex_match(message.text, place, PlacePattern()))
				{
					// A warning about the engine's GLSL is nothing the file can do
					// anything about.
					if (std::stoi(place[1]) != FILE_SOURCE && !message.error)
						continue;

					if (std::stoi(place[1]) == FILE_SOURCE)
						message.line = std::stoul(place[2]);

					message.text = place[3];
				};

				// COMMON is in both stages, and so is anything wrong with it.
				const bool repeated = std::any_of(
					messages.begin(),
					messages.end(),
					[&message](const ShaderMessage& other) { return other.line == message.line && other.text == message.text; });

				if (!repeated)
					messages.push_back(std::move(message));
			};

			return messages;
		};
#endif
	};

	Shader::Shader() :
		id(CompileSource(defaultShader))
	{ };

	Shader::Shader(const std::string& file_path) :
		file_path(file_path.ends_with(extension) ?
			file_path :
			file_path + extension),
		id(CompileSource(file_path))
	{ };

	Shader::Shader(const std::string& name, std::istream& source) :
		file_path(name),
		id(CompileStream(name, source))
	{ };

	Shader::~Shader()
	{
		std::scoped_lock lock{ mutex };

		shaders.erase(file_path);

		if (Renderer* renderer = Renderer::Get())
		{
			for (const uint32_t program : { id, shadow_id, instanced_id, shadow_instanced_id })
				if (program)
					renderer->DeleteProgram(program);
		};
	};

	enum class ShaderType { NONE, COMMON, VERTEX, FRAGMENT, SETTINGS };

	uint32_t Shader::CompileSource(const std::string& file_path)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(file_path))
			return Cached(shaders[file_path]);

		std::istringstream file(ReadSource(file_path));

		return Build(file_path, file);
	};

	std::string Shader::ReadSource(const std::string& file_path)
	{
#if __EMSCRIPTEN__
		std::string source;
		Request(file_path, source);
		return source;
#else
		std::ifstream file(file_path);
		if (!file)
			throw std::runtime_error("could not open the file");

		std::error_code code;
		m_written = std::filesystem::last_write_time(file_path, code);

		return std::string(std::istreambuf_iterator<char>(file), { });
#endif
	};

	bool Shader::Reload()
	{
		std::scoped_lock lock{ mutex };

		const uint32_t previous[] = { id, shadow_id, instanced_id, shadow_instanced_id };

		try
		{
			std::istringstream source(ReadSource(file_path));
			id = Build(file_path, source);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Shader: " << file_path << ": " << e.what() << std::endl;
			return false;
		};

		if (Renderer* renderer = Renderer::Get())
			for (const uint32_t program : previous)
				if (program)
					renderer->DeleteProgram(program);

		return true;
	};

	void Shader::ReloadIfChanged()
	{
		std::error_code code;
		const std::filesystem::file_time_type written = std::filesystem::last_write_time(file_path, code);

		if (!code && written != m_written)
			Reload();
	};

	uint32_t Shader::Cached(const Programs& programs)
	{
		settings = programs.settings;
		shadow_id = programs.shadow;
		instanced_id = programs.instanced;
		shadow_instanced_id = programs.shadowInstanced;
		vertex = programs.vertex;

		return programs.id;
	};

	uint32_t Shader::CompileStream(const std::string& name, std::istream& source)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(name))
			return Cached(shaders[name]);

		return Build(name, source);
	};

	Shader::Stages Shader::Assemble(const std::string& source)
	{
		std::istringstream lines(source);

		std::unordered_map<ShaderType, std::stringstream> sections;
		ShaderType current = ShaderType::NONE;
		bool has_vertex = false;

		size_t number = 0;

		// Each section starts on the file's own line number, so what the
		// compiler says about a line of a stage is about that line of the file.
		for (std::string line; std::getline(lines, line);)
		{
			number++;

			if (line.find(VERTEX_SECTION) != std::string::npos)
			{
				current = ShaderType::VERTEX;
				has_vertex = true;
			}
			else if (line.find(FRAGMENT_SECTION) != std::string::npos)
				current = ShaderType::FRAGMENT;
			else if (line.find(COMMON_SECTION) != std::string::npos)
				current = ShaderType::COMMON;
			else if (line.find(SETTINGS_SECTION) != std::string::npos)
				current = ShaderType::SETTINGS;
			else
			{
				if (current != ShaderType::NONE)
					sections[current] << line << '\n';

				continue;
			};

			sections[current] << LineDirective(number + 1, FILE_SOURCE);
		};

		Stages stages;
		stages.settings = ParseShaderSettings(source);

		const ShaderSettings& settings = stages.settings;
		const std::string common = sections[ShaderType::COMMON].str();
		const std::string vertex = common + (has_vertex ? sections[ShaderType::VERTEX].str() : EngineCode(DEFAULT_VERTEX));
		const std::string fragment = common + sections[ShaderType::FRAGMENT].str();

		stages.vertex = settings.shading ? ShadedVertex(vertex, settings.shadows) : vertex;
		stages.fragment = settings.shading ? ShadedFragment(fragment, settings.shadows) : fragment;

		if (settings.shadows)
		{
			stages.shadowVertex = vertex;
			stages.shadowFragment = settings.transparency ? TransparentShadowFragment(fragment) : common + EngineCode(SHADOW_FRAGMENT);
		};

		// The engine's GLSL goes first, so a shader's own COMMON block (its
		// precision statements included) has the last word.
		for (std::string* stage : { &stages.vertex, &stages.shadowVertex })
			if (!stage->empty())
				*stage = EngineCode(Light::VertexLibrary(*stage)) + *stage;

		for (std::string* stage : { &stages.fragment, &stages.shadowFragment })
			if (!stage->empty())
				*stage = EngineCode(Light::FragmentLibrary(*stage)) + *stage;

		return stages;
	};

	uint32_t Shader::Build(const std::string& name, std::istream& source)
	{
		const std::string text(std::istreambuf_iterator<char>(source), { });

		Renderer* renderer = Renderer::Get();

		if (!renderer)
			throw std::runtime_error("No renderer to compile " + name + " with");

		const Stages stages = Assemble(text);

		Programs programs;
		programs.settings = stages.settings;
		programs.vertex = stages.vertex;
		programs.id = renderer->CreateProgram(name, stages.vertex, stages.fragment);
		renderer->SetPixelated(programs.id, stages.settings.pixelated);

		if (stages.settings.shadows)
			try
			{
				programs.shadow = renderer->CreateProgram(name + SHADOW_PROGRAM_SUFFIX, stages.shadowVertex, stages.shadowFragment);
			}
			catch (...)
			{
				renderer->DeleteProgram(programs.id);
				throw;
			};

		programs.instanced = CreateIfCompiles(*renderer, name + INSTANCED_PROGRAM_SUFFIX, Instanced(stages.vertex, stages.fragment));

		if (stages.settings.shadows)
			programs.shadowInstanced = CreateIfCompiles(*renderer, name + SHADOW_PROGRAM_SUFFIX + INSTANCED_PROGRAM_SUFFIX, Instanced(stages.shadowVertex, stages.shadowFragment));

		// After the programs, so a reload that fails leaves these describing
		// the ones still in use.
		variables = ParseShaderVariables(text);
		shaders[name] = programs;

		return Cached(programs);
	};

#ifndef __EMSCRIPTEN__
	std::vector<ShaderMessage> Shader::Check(const std::string& text)
	{
		Stages stages;

		try
		{
			stages = Assemble(text);
		}
		catch (const std::runtime_error& e)
		{
			return { ShaderMessage{ 0, true, e.what() } };
		};

		std::string log = Glsl::Compile(stages.vertex, stages.fragment).log;

		if (stages.settings.shadows)
			log += Glsl::Compile(stages.shadowVertex, stages.shadowFragment).log;

		return ParseMessages(log);
	};
#endif

#ifdef __EMSCRIPTEN__
	void Shader::Request(
		const std::string& file_path,
		std::string& shader_source)
	{
		bool fetchComplete = false;
		bool fetchSuccess = false;

		emscripten_fetch_attr_t attr;
		emscripten_fetch_attr_init(&attr);
		strcpy(attr.requestMethod, "GET");
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;

		// Success callback
		attr.onsuccess = [](emscripten_fetch_t* fetch)
		{
			auto* fetchData = reinterpret_cast<std::pair<std::string*, bool*>*>(fetch->userData);
			std::string* target = fetchData->first;
			*target = std::string(fetch->data, fetch->numBytes);
			*fetchData->second = true; // Mark fetch as successful
			emscripten_fetch_close(fetch);
			delete fetchData;
		};

		// Error callback
		attr.onerror = [](emscripten_fetch_t* fetch)
		{
			auto* fetchData = reinterpret_cast<std::pair<std::string*, bool*>*>(fetch->userData);
			*fetchData->second = true; // Mark fetch as complete (even on error)
			emscripten_fetch_close(fetch);
			delete fetchData;
		};

		// Pass the shader_source and fetchComplete flag as user data
		attr.userData = new std::pair<std::string*, bool*>(&shader_source, &fetchComplete);
		emscripten_fetch(&attr, file_path.c_str());

		// Busy-wait until fetch is complete
		while (!fetchComplete)
			emscripten_sleep(10); // Yield control to the browser (non-blocking wait)

		// Thrown like a file that will not open, so a Material or a reload
		// reports it once, with the path.
		if (shader_source.empty())
			throw std::runtime_error("fetched nothing");
	};
#endif
};

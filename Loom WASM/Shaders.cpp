#include "Shaders.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include "OpenGL.h"

#include <iostream>
#include <fstream>
#include <filesystem>

#define SHADER_PATH ""


namespace Loom
{
	static inline std::string defaultShader;

	Shader::Shader() :
		id(CompileSource(defaultShader))
	{ };

	Shader::Shader(const std::string& file_path) :
		file_path(file_path.ends_with(".shader") ?
			file_path :
			file_path + ".shader"),
		id(CompileSource(file_path))
	{ };

	Shader::~Shader()
	{
		std::scoped_lock lock{ mutex };

		shaders.erase(file_path);
		glDeleteShader(id);
	};

#if __EMSCRIPTEN
#define VERSION "#version 300 es"
#else
#define VERSION "#version 460 core"
#endif

	enum class ShaderType { NONE, COMMON, VERTEX, FRAGMENT, GEOMETRY, COMPUTE };

	static inline std::unordered_map<std::string, GLenum> ShaderMap = {
		{"VERTEX", GL_VERTEX_SHADER},
		{"FRAGMENT", GL_FRAGMENT_SHADER},
		{"GEOMETRY", GL_GEOMETRY_SHADER},
		{"COMPUTE", GL_COMPUTE_SHADER}
	};

	uint32_t Shader::CompileSource(const std::string& file_path)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(file_path))
			return shaders[file_path];

#if __EMSCRIPTEN__
		Request(m_file_path, shader_raw);
#endif

		std::ifstream file(file_path);
		if (!file)
			throw std::runtime_error("Could not open shader file: " + file_path);

		std::unordered_map<std::string, std::stringstream> sources;
		ShaderType current = ShaderType::NONE;

		std::string line;
		while (std::getline(file, line))
			if (line.find("===VERTEX===") != std::string::npos)
				current = ShaderType::VERTEX;
			else if (line.find("===FRAGMENT===") != std::string::npos)
				current = ShaderType::FRAGMENT;
			else if (line.find("===GEOMETRY===") != std::string::npos)
				current = ShaderType::GEOMETRY;
			else if (line.find("===COMPUTE===") != std::string::npos)
				current = ShaderType::COMPUTE;
			else if (line.find("===COMMON===") != std::string::npos)
				current = ShaderType::COMMON;
			else if (current != ShaderType::NONE)
				sources[std::to_string(static_cast<int>(current))] << line << '\n';

		std::string commonCode = sources.contains(std::to_string(static_cast<int>(ShaderType::COMMON))) ?
			sources[std::to_string(static_cast<int>(ShaderType::COMMON))].str() : "";

		std::vector<GLuint> shaderObjects;
		GLuint program = glCreateProgram();

		for (const auto& [typeStr, glShaderType] : ShaderMap)
		{
			auto key = std::to_string(static_cast<int>(
				typeStr == "VERTEX" ? ShaderType::VERTEX :
				typeStr == "FRAGMENT" ? ShaderType::FRAGMENT :
				typeStr == "GEOMETRY" ? ShaderType::GEOMETRY :
				ShaderType::COMPUTE));

			if (!sources.contains(key))
				continue;

			std::string shaderSrc = std::string(VERSION) + "\n" + commonCode + sources[key].str();
			const char* srcPtr = shaderSrc.c_str();

			GLuint shader = glCreateShader(glShaderType);
			glShaderSource(shader, 1, &srcPtr, nullptr);
			glCompileShader(shader);

			GLint success;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				char log[512];
				glGetShaderInfoLog(shader, 512, nullptr, log);
				throw std::runtime_error("Shader compile error in " + typeStr + ":\n" + log);
			};

			glAttachShader(program, shader);
			shaderObjects.push_back(shader);
		};

		glLinkProgram(program);

		GLint success;
		glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success)
		{
			char log[512];
			glGetProgramInfoLog(program, 512, nullptr, log);
			throw std::runtime_error("Shader link error:\n" + std::string(log));
		};

		for (GLuint shader : shaderObjects)
			glDeleteShader(shader);

		shaders.emplace(file_path, program);

		return program;
	};

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
			std::cerr << "Failed to fetch: " << fetch->url << std::endl;
			emscripten_fetch_close(fetch);
			delete fetchData;
		};

		// Pass the shader_source and fetchComplete flag as user data
		attr.userData = new std::pair<std::string*, bool*>(&shader_source, &fetchComplete);
		emscripten_fetch(&attr, file_path.c_str());

		// Busy-wait until fetch is complete
		while (!fetchComplete)
			emscripten_sleep(10); // Yield control to the browser (non-blocking wait)

		if (shader_source.empty())
			std::cerr << "Error: Fetch failed or returned no data." << std::endl;
	};
#endif
};

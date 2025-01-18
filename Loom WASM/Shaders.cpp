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
	static inline std::string defaultVertexShader =
#ifdef __EMSCRIPTEN__
		R"(#version 300 es
    layout(location = 0) in vec3 aPos;
    void main() {
        gl_Position = vec4(aPos, 1.0f);
    })";
#else
		R"(#version 460 core
	layout(location = 0) in vec3 aPos;
	void main() {
		gl_Position = vec4(aPos, 1.0f);
	})";
#endif

	static inline std::string defaultFragmentShader =
#ifdef __EMSCRIPTEN__
		R"(#version 300 es
	precision mediump float; // Add this line for WebGL compatibility
    out vec4 FragColor;
    void main() {
        FragColor = vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green color
    })";
#else
		R"(#version 460 core
	out vec4 FragColor;
	void main() {
		FragColor = vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green color
	})";
#endif

	// TODO: Add geometry and tessellation shaders

	Shader::Shader() :
		m_id(CompileSource(
			defaultVertexShader,
			defaultFragmentShader))
	{ };

	Shader::Shader(const std::string& file_path) :
		m_file_path(file_path)
	{
		std::string vertex_shader_source;
		std::string fragment_shader_source;

#if __EMSCRIPTEN__
		Request(m_file_path + ".vert", vertex_shader_source);
		Request(m_file_path + ".frag", fragment_shader_source);
#else
		std::string vertex_shader_path = SHADER_PATH + m_file_path + ".vert";
		std::string fragment_shader_path = SHADER_PATH + m_file_path + ".frag";
		std::ifstream vertex_shader_file(vertex_shader_path);
		std::ifstream fragment_shader_file(fragment_shader_path);
		if (vertex_shader_file.is_open())
		{
			std::string line;
			while (std::getline(vertex_shader_file, line))
				vertex_shader_source += line + '\n';
		}
		else
		{
			std::cout << "Failed to open vertex shader file: " << vertex_shader_path << std::endl;
			return;
		};
		if (fragment_shader_file.is_open())
		{
			std::string line;
			while (std::getline(fragment_shader_file, line))
				fragment_shader_source += line + '\n';
		}
		else
		{
			std::cout << "Failed to open fragment shader file: " << fragment_shader_path << std::endl;
			return;
		};
#endif

		m_id = CompileSource(
			vertex_shader_source,
			fragment_shader_source);
	};

	Shader::~Shader()
	{
		glDeleteShader(m_id);
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

	void Shader::GetShaderVec2(
		const std::string& name,
		void*& data)
	{
		glUseProgram(m_id);

		GLuint location = glGetUniformLocation(m_id, name.c_str());

		glGetUniformfv(
			m_id,
			location,
			(float*)data);
	};

	void Shader::SetShaderVec2(
		const std::string& name,
		const void* data,
		bool shouldUpdate)
	{
		glUseProgram(m_id);

		GLuint location = glGetUniformLocation(m_id, name.c_str());

		glUniform2fv(
			location,
			1,
			(float*)data);

		if (shouldUpdate)
			m_uniforms[m_id][ID_Type(location, 2, GL_FLOAT).as_uint64()] = data;
	};
	
	void Shader::SetShaderVec3(
		const std::string& name,
		const void* data,
		bool shouldUpdate)
	{
		glUseProgram(m_id);

		GLuint location = glGetUniformLocation(m_id, name.c_str());

		glUniform3fv(
			location,
			1,
			(float*)data);

		if (shouldUpdate)
			m_uniforms[m_id][ID_Type(location, 3, GL_FLOAT).as_uint64()] = data;
	};

	void Shader::SetShaderMat4(
		const std::string& name,
		const void* data,
		bool shouldUpdate)
	{
		glUseProgram(m_id);

		GLuint location = glGetUniformLocation(m_id, name.c_str());

		glUniformMatrix4fv(
			location,
			1,
			GL_FALSE,
			(float*)data);

		if (shouldUpdate)
			m_uniforms[m_id][ID_Type(location, 16, GL_FLOAT).as_uint64()] = data;
	};

	void Shader::SetUniforms()
	{
		for (auto& i : m_uniforms)
		{
			glUseProgram(i.first);

			for (auto& j : i.second)
			{

			};
		};
	};

	uint32_t Shader::CompileSource(
		const std::string& vertex_shader_source,
		const std::string& fragment_shader_source)
	{
		const char* vertex_shader_source_c = vertex_shader_source.c_str();
		const char* fragment_shader_source_c = fragment_shader_source.c_str();
		unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertex_shader, 1, &vertex_shader_source_c, NULL);
		glCompileShader(vertex_shader);
		int success;
		char infoLog[512];
		glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(vertex_shader, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
		};
		unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragment_shader, 1, &fragment_shader_source_c, NULL);
		glCompileShader(fragment_shader);
		glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(fragment_shader, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
		};
		GLuint shader_id = glCreateProgram();
		glAttachShader(shader_id, vertex_shader);
		glAttachShader(shader_id, fragment_shader);
		glLinkProgram(shader_id);
		glGetProgramiv(shader_id, GL_LINK_STATUS, &success);
		if (!success)
		{
			glGetProgramInfoLog(shader_id, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		};
		glDeleteShader(vertex_shader);
		glDeleteShader(fragment_shader);

		return shader_id;
	};
};

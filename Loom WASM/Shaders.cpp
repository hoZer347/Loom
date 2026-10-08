#include "Shaders.h"

#include "Light.h"
#include "Renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include <sstream>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iterator>

#define SHADER_PATH ""


namespace Loom
{
	static inline std::string defaultShader;

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
			renderer->DeleteProgram(id);
	};

	enum class ShaderType { NONE, COMMON, VERTEX, FRAGMENT };

	uint32_t Shader::CompileSource(const std::string& file_path)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(file_path))
			return shaders[file_path];

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
			throw std::runtime_error("Could not open shader file: " + file_path);

		std::error_code code;
		m_written = std::filesystem::last_write_time(file_path, code);

		return std::string(std::istreambuf_iterator<char>(file), { });
#endif
	};

	bool Shader::Reload()
	{
		std::scoped_lock lock{ mutex };

		const uint32_t previous = id;

		try
		{
			std::istringstream source(ReadSource(file_path));
			id = Build(file_path, source);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Shader: " << e.what() << std::endl;
			return false;
		};

		if (Renderer* renderer = Renderer::Get())
			renderer->DeleteProgram(previous);

		return true;
	};

	void Shader::ReloadIfChanged()
	{
		std::error_code code;
		const std::filesystem::file_time_type written = std::filesystem::last_write_time(file_path, code);

		if (!code && written != m_written)
			Reload();
	};

	uint32_t Shader::CompileStream(const std::string& name, std::istream& source)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(name))
			return shaders[name];

		return Build(name, source);
	};

	uint32_t Shader::Build(const std::string& name, std::istream& source)
	{
		const std::string text(std::istreambuf_iterator<char>(source), { });
		std::istringstream lines(text);

		std::unordered_map<ShaderType, std::stringstream> sources;
		ShaderType current = ShaderType::NONE;

		std::string line;
		while (std::getline(lines, line))
			if (line.find(VERTEX_SECTION) != std::string::npos)
				current = ShaderType::VERTEX;
			else if (line.find(FRAGMENT_SECTION) != std::string::npos)
				current = ShaderType::FRAGMENT;
			else if (line.find(COMMON_SECTION) != std::string::npos)
				current = ShaderType::COMMON;
			else if (current != ShaderType::NONE)
				sources[current] << line << '\n';

		Renderer* renderer = Renderer::Get();

		if (!renderer)
			throw std::runtime_error("No renderer to compile " + name + " with");

		// The engine's GLSL goes first, so a shader's own COMMON block (its
		// precision statements included) has the last word.
		const std::string common = sources[ShaderType::COMMON].str();
		const std::string vertex = common + sources[ShaderType::VERTEX].str();
		const std::string fragment = common + sources[ShaderType::FRAGMENT].str();

		const uint32_t program = renderer->CreateProgram(
			name,
			Light::VertexLibrary(vertex) + vertex,
			Light::FragmentLibrary(fragment) + fragment);

		// After the program, so a reload that fails leaves them describing the
		// program still in use.
		variables = ParseShaderVariables(text);
		shaders[name] = program;

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

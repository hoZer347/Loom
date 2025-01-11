#include "Shaders.h"

#include "OpenGL.h"

#include <iostream>
#include <filesystem>

#define SHADER_PATH "shaders/"


namespace Loom
{
	struct Shader final
	{
		Shader()
		{ };

		Shader(const std::string& file_path) :
			file_path(file_path)
		{ };

		~Shader()
		{
			glDeleteShader(id);
		};

		const std::string file_path;

		uint32_t id = 0;
	};

	void PrepareShaders()
	{

	};
};

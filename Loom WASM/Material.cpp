#include "Material.h"

#include "Shaders.h"

#include <iostream>
#include <string>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		// One Shader per path, shared by every Material that names it. Materials
		// deliberately do not own theirs: ~Shader erases the entry in Shader's own
		// program cache, so the first Material destroyed would pull the compiled
		// program out from under the rest. These live until the process ends.
		Shader* Shared(const std::string& path)
		{
			static std::unordered_map<std::string, Shader*> shaders{ };

			const auto found = shaders.find(path);

			if (found != shaders.end())
				return found->second;

			try
			{
				return shaders[path] = new Shader(path);
			}
			catch (const std::exception& e)
			{
				std::cerr << "Material: " << e.what() << std::endl;

				// Deliberately not remembered: fixing the shader file and
				// reloading the scene should be enough, without restarting the
				// editor. Shader's own registry does the same.
				return nullptr;
			};
		};
	};

	void Material::OnAttach()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Shared(m_shader_path);
	};
};

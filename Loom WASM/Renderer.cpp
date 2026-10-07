#include "Renderer.h"


namespace Loom
{
	std::unique_ptr<Renderer> CreateOpenGLRenderer();
#ifndef __EMSCRIPTEN__
	std::unique_ptr<Renderer> CreateVulkanRenderer();
#endif

	std::unique_ptr<Renderer> Renderer::Create(Backend backend)
	{
#ifndef __EMSCRIPTEN__
		if (backend == Backend::Vulkan)
			return CreateVulkanRenderer();
#endif

		return CreateOpenGLRenderer();
	};
};

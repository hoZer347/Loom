#include "Renderer.h"


namespace Loom
{
	size_t FloatsPerVertex(VertexFormat format)
	{
		constexpr size_t POSITION = 3;
		constexpr size_t UV = 2;
		constexpr size_t NORMAL = 3;

		switch (format)
		{
		case VertexFormat::PositionUV:
			return POSITION + UV;
		case VertexFormat::PositionUVNormal:
			return POSITION + UV + NORMAL;
		default:
			return POSITION;
		};
	};

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

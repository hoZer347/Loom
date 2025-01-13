#pragma once

#include "Component.h"
#include "OpenGL.h"


namespace Loom
{
	struct Shader;

	struct Material : Component<Material>
	{
		void OnRender() override
		{

		};

		Shader* m_shader;
	};
};

#pragma once

#include "GameObject.h"

#include <glm/glm.hpp>
using namespace glm;


namespace Loom
{
	template <typename T>
	struct Geometry
	{
		vec3 position	{ 0.0f, 0.0f, 0.0f };
		vec3 rotation	{ 0.0f, 0.0f, 0.0f };
		vec3 scale		{ 1.0f, 1.0f, 1.0f };

		bool shouldUpdate = false;

		void Render()
		{
			if (!shouldUpdate) return;


		};
	};
};

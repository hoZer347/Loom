#pragma once

namespace Loom
{
	struct alignas(sizeof(float) * 2) vec2
	{
		float data[2];
	};

	struct vec3
	{
		vec3(float x, float y, float z)
		{
			data[0] = x;
			data[1] = y;
			data[2] = z;
		};

		float data[3];
	};

	struct alignas(sizeof(float) * 4) vec4
	{
		float data[4];
	};

	struct alignas(sizeof(float) * 16) mat4
	{
		float data[16];
	};
};

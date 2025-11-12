#pragma once

namespace Loom
{
namespace Math
{
	template <typename T = float>
	struct alignas(sizeof(T) * 2) vec2
	{
		T data[2];
	};

	template <typename T = float>
	struct vec3
	{
		vec3(const T& x, const T& y, const T& z)
		{
			data[0] = x;
			data[1] = y;
			data[2] = z;
		};

		T data[3];
	};

	template <typename T = float>
	struct alignas(sizeof(T) * 4) vec4
	{
		T data[4];
	};

	template <typename T = float>
	struct alignas(sizeof(T) * 16) mat4
	{
		T data[16];
	};
};
};

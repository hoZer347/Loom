#pragma once

#include <iterator>
#include <string_view>


namespace Loom
{
	// Every type a shader can declare a loose uniform as that both desktop GLSL
	// and GLSL ES 3.00 have, and that the renderers can put a value behind.
	enum class UniformType
	{
		Float, Vec2, Vec3, Vec4,
		Int, IVec2, IVec3, IVec4,
		UInt, UVec2, UVec3, UVec4,
		Bool, BVec2, BVec3, BVec4,
		Mat2, Mat2x3, Mat2x4,
		Mat3x2, Mat3, Mat3x4,
		Mat4x2, Mat4x3, Mat4,
		Sampler2D,
	};

	// What each component is held as. A bool is handed over as an int.
	enum class UniformKind { Float, Int, UInt, Bool, Texture };

	struct UniformTypeInfo
	{
		UniformType type;
		const char* glsl;
		UniformKind kind;

		// GLSL's matCxR has C columns of R rows; a vector is one column.
		int columns;
		int rows;
	};

	// In UniformType's order, so a type indexes its own row.
	inline constexpr UniformTypeInfo UNIFORM_TYPES[] =
	{
		{ UniformType::Float,		"float",		UniformKind::Float,		1, 1 },
		{ UniformType::Vec2,		"vec2",			UniformKind::Float,		1, 2 },
		{ UniformType::Vec3,		"vec3",			UniformKind::Float,		1, 3 },
		{ UniformType::Vec4,		"vec4",			UniformKind::Float,		1, 4 },
		{ UniformType::Int,			"int",			UniformKind::Int,		1, 1 },
		{ UniformType::IVec2,		"ivec2",		UniformKind::Int,		1, 2 },
		{ UniformType::IVec3,		"ivec3",		UniformKind::Int,		1, 3 },
		{ UniformType::IVec4,		"ivec4",		UniformKind::Int,		1, 4 },
		{ UniformType::UInt,		"uint",			UniformKind::UInt,		1, 1 },
		{ UniformType::UVec2,		"uvec2",		UniformKind::UInt,		1, 2 },
		{ UniformType::UVec3,		"uvec3",		UniformKind::UInt,		1, 3 },
		{ UniformType::UVec4,		"uvec4",		UniformKind::UInt,		1, 4 },
		{ UniformType::Bool,		"bool",			UniformKind::Bool,		1, 1 },
		{ UniformType::BVec2,		"bvec2",		UniformKind::Bool,		1, 2 },
		{ UniformType::BVec3,		"bvec3",		UniformKind::Bool,		1, 3 },
		{ UniformType::BVec4,		"bvec4",		UniformKind::Bool,		1, 4 },
		{ UniformType::Mat2,		"mat2",			UniformKind::Float,		2, 2 },
		{ UniformType::Mat2x3,		"mat2x3",		UniformKind::Float,		2, 3 },
		{ UniformType::Mat2x4,		"mat2x4",		UniformKind::Float,		2, 4 },
		{ UniformType::Mat3x2,		"mat3x2",		UniformKind::Float,		3, 2 },
		{ UniformType::Mat3,		"mat3",			UniformKind::Float,		3, 3 },
		{ UniformType::Mat3x4,		"mat3x4",		UniformKind::Float,		3, 4 },
		{ UniformType::Mat4x2,		"mat4x2",		UniformKind::Float,		4, 2 },
		{ UniformType::Mat4x3,		"mat4x3",		UniformKind::Float,		4, 3 },
		{ UniformType::Mat4,		"mat4",			UniformKind::Float,		4, 4 },
		{ UniformType::Sampler2D,	"sampler2D",	UniformKind::Texture,	1, 1 },
	};

	// A mat4's worth.
	inline constexpr int MAX_UNIFORM_COMPONENTS = 16;

	inline constexpr const UniformTypeInfo& InfoOf(UniformType type)
	{
		return UNIFORM_TYPES[(int)type];
	};

	inline constexpr bool IsMatrix(const UniformTypeInfo& info)
	{
		return info.columns > 1;
	};

	// By the GLSL spelling. GLSL's matNxN is its matN.
	inline bool FindUniformType(std::string_view glsl, UniformType& type)
	{
		constexpr std::string_view SQUARE[] = { "mat2x2", "mat3x3", "mat4x4" };
		constexpr UniformType SQUARE_TYPES[] = { UniformType::Mat2, UniformType::Mat3, UniformType::Mat4 };

		for (size_t i = 0; i < std::size(SQUARE); i++)
			if (glsl == SQUARE[i])
			{
				type = SQUARE_TYPES[i];
				return true;
			};

		for (const UniformTypeInfo& info : UNIFORM_TYPES)
			if (glsl == info.glsl)
			{
				type = info.type;
				return true;
			};

		return false;
	};
};

#include "doctest.h"

// glslang is only in the desktop build.
#ifndef __EMSCRIPTEN__

#include "GlslCompiler.h"

#include <cstdint>
#include <string>
#include <vector>


namespace
{
	std::vector<uint32_t> BindingsIn(const std::vector<uint32_t>& spirv)
	{
		constexpr size_t HEADER_WORDS = 5;
		constexpr uint32_t OPCODE_MASK = 0xFFFF;
		constexpr uint32_t COUNT_SHIFT = 16;
		constexpr uint32_t OP_DECORATE = 71;
		constexpr uint32_t DECORATION_BINDING = 33;
		constexpr size_t DECORATION_WORD = 2;
		constexpr size_t VALUE_WORD = 3;

		std::vector<uint32_t> bindings;

		for (size_t i = HEADER_WORDS; i < spirv.size();)
		{
			const uint32_t opcode = spirv[i] & OPCODE_MASK;
			const uint32_t count = spirv[i] >> COUNT_SHIFT;

			if (opcode == OP_DECORATE && count > VALUE_WORD && spirv[i + DECORATION_WORD] == DECORATION_BINDING)
				bindings.push_back(spirv[i + VALUE_WORD]);

			i += count == 0 ? 1 : count;
		};

		return bindings;
	};
};


TEST_SUITE("GlslCompiler")
{
	TEST_CASE("a stage with samplers and no loose uniforms keeps them off the other stage's block")
	{
		const Loom::Glsl::Program program = Loom::Glsl::Compile(
			"uniform float Scale;\n"
			"layout(location = 0) in vec3 aPos;\n"
			"void main() { gl_Position = vec4(aPos * Scale, 1.0); }\n",
			"uniform highp sampler2D Counter;\n"
			"uniform highp sampler2D Counter1;\n"
			"out vec4 FragColor;\n"
			"void main() { FragColor = texelFetch(Counter, ivec2(0), 0) + texelFetch(Counter1, ivec2(0), 0); }\n");

		INFO(program.log);
		REQUIRE(program.compiled);

		const std::vector<uint32_t> samplers = BindingsIn(program.spirv[1]);

		CHECK(samplers.size() == 2);
		for (const uint32_t binding : samplers)
			CHECK(binding != Loom::Glsl::DEFAULT_BLOCK_BINDING);
	};
};

#endif

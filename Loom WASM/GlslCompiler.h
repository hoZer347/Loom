#pragma once

#ifndef __EMSCRIPTEN__

#include <array>
#include <cstdint>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Glsl
	* - Compiles a program's vertex and fragment stages to SPIR-V with glslang,
	*   GL's rules relaxed onto Vulkan's: loose uniforms are gathered into one
	*   block, and bindings and locations are given out across the two stages
	*   together
	* - What the Vulkan renderer draws with, and what the editor checks a
	*   shader with whichever renderer is running
	*/
	namespace Glsl
	{
		// Vertex and fragment.
		constexpr size_t STAGES = 2;

		// Loose uniforms are gathered into one block, at this binding.
		constexpr uint32_t DEFAULT_BLOCK_BINDING = 0;

		// The fragment stage's specialisation constant that undoes the window's
		// flipped viewport for dFdy.
		constexpr uint32_t FLIP_CONSTANT_ID = 0;

		struct Program
		{
			bool compiled = false;
			std::array<std::vector<uint32_t>, STAGES> spirv;

			// glslang's messages for both stages and the link, one a line, each
			// as "ERROR: <source string>:<line>: <message>" when it has a place.
			std::string log;
		};

		// Both stages are parsed even when the first fails, so the log has
		// everything wrong with either.
		Program Compile(const std::string& vertex, const std::string& fragment);
	};
};

#endif

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Glsl
	* - Compiles a program's stages to SPIR-V with glslang, GL's rules relaxed
	*   onto Vulkan's: loose uniforms are gathered into one block, and bindings
	*   and locations are given out across the stages together
	* - Reflect reads back what a program binds, and refuses what any backend
	*   could not bind, so a shader fails the same way on all of them
	* - What the Vulkan and WebGPU renderers draw with, what desktop GL checks
	*   its storage buffers with, and what the editor checks a shader with
	*   whichever renderer is running
	*/
	namespace Glsl
	{
		// Loose uniforms are gathered into one block, at this binding.
		constexpr uint32_t DEFAULT_BLOCK_BINDING = 0;

		// The fragment stage's specialisation constant that undoes the window's
		// flipped viewport for dFdy.
		constexpr uint32_t FLIP_CONSTANT_ID = 0;

		// Which stages read a resource, a bit each.
		constexpr uint32_t VERTEX_STAGE = 1;
		constexpr uint32_t FRAGMENT_STAGE = 2;
		constexpr uint32_t COMPUTE_STAGE = 4;

		struct Program
		{
			bool compiled = false;

			// The vertex stage then the fragment, or the compute stage alone.
			std::vector<std::vector<uint32_t>> spirv;

			// glslang's messages for every stage and the link, one a line, each
			// as "ERROR: <source string>:<line>: <message>" when it has a place.
			std::string log;
		};

		// Both stages are parsed even when the first fails, so the log has
		// everything wrong with either.
		Program Compile(const std::string& vertex, const std::string& fragment);
		Program CompileCompute(const std::string& compute);

		struct Layout
		{
			struct Member
			{
				uint32_t offset;
				uint32_t size;

				// Between a matrix's columns, which std140 pads to a vec4 each.
				uint32_t matrixStride;
			};

			struct Sampler
			{
				uint32_t binding;
				bool shadow;
				uint32_t stages;
			};

			struct Binding
			{
				uint32_t binding;
				uint32_t stages;
				bool readonly;
			};

			uint32_t blockSize = 0;
			uint32_t blockStages = 0;
			std::map<std::string, Member> members;
			std::map<std::string, Sampler> samplers;

			// Storage blocks by their type name, and images by theirs.
			std::map<std::string, Binding> buffers;
			std::map<std::string, Binding> images;

			// The vertex stage's input locations.
			std::vector<uint32_t> inputs;

			std::array<uint32_t, 3> localSize{ };
		};

		// Throws std::runtime_error naming the program for anything Loom does
		// not bind: a resource of another kind, an image that is not a
		// writeonly rgba8 image2D or one outside a compute stage, or a storage
		// block a vertex stage could write.
		Layout Reflect(const std::string& name, const Program& program);
	};
};

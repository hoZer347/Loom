#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Wgsl
	* - Converts SPIR-V to WGSL with Tint, for the WebGPU renderer
	* - WGSL has no combined image samplers, so each one splits into a texture
	*   that keeps its binding and a sampler at the same binding in
	*   samplerGroup
	*/
	namespace Wgsl
	{
		// Throws std::runtime_error with Tint's message when it cannot.
		std::string FromSpirv(const std::vector<uint32_t>& spirv, const std::vector<uint32_t>& samplerBindings, uint32_t samplerGroup);
	};
};

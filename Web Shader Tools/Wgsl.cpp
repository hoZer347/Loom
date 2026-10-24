#include "Wgsl.h"

#include "src/tint/lang/core/ir/module.h"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"

#include <stdexcept>


namespace Loom::Wgsl
{
	namespace
	{
		// Glsl gathers everything a program binds into this group.
		constexpr uint32_t RESOURCE_GROUP = 0;
	};

	std::string FromSpirv(const std::vector<uint32_t>& spirv, const std::vector<uint32_t>& samplerBindings, uint32_t samplerGroup)
	{
		tint::spirv::reader::Options readOptions;

		for (uint32_t binding : samplerBindings)
			readOptions.sampler_mappings[tint::BindingPoint{ RESOURCE_GROUP, binding }] = tint::BindingPoint{ samplerGroup, binding };

		auto module = tint::spirv::reader::ReadIR(spirv, readOptions);

		if (module != tint::Success)
			throw std::runtime_error(module.Failure().reason);

		// GLSL lets a derivative sit under any branch.
		tint::wgsl::writer::Options writeOptions;
		writeOptions.allow_non_uniform_derivatives = true;

		auto wgsl = tint::wgsl::writer::WgslFromIR(module.Get(), writeOptions);

		if (wgsl != tint::Success)
			throw std::runtime_error(wgsl.Failure().reason);

		return wgsl->wgsl;
	};
};

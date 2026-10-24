#include "GlslCompiler.h"

#include "spirv_reflect.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <memory>
#include <stdexcept>


namespace Loom::Glsl
{
	namespace
	{
		constexpr int GLSL_VERSION = 460;

		// The Vulkan GLSL semantics glslang compiles to (the VULKAN macro's value).
		constexpr int VULKAN_INPUT_VERSION = 100;

#ifdef __EMSCRIPTEN__
		// The newest Tint reads.
		constexpr glslang::EShTargetLanguageVersion SPIRV_VERSION = glslang::EShTargetSpv_1_3;
#else
		constexpr glslang::EShTargetLanguageVersion SPIRV_VERSION = glslang::EShTargetSpv_1_6;
#endif

		// Storage blocks lay out as std430 unless they say otherwise, as they do
		// under Vulkan and WebGPU.
		const char* const PRELUDE = "#version 460 core\nlayout(std430) buffer;\n";

		// #line keeps the compiler's line numbers where GL's would be.
		const char* const LINE_RESET = "#line 2\n";

		// The window is drawn with a flipped viewport, so its rows come out top
		// first while clip space keeps GL's y up. That reverses screen space y,
		// which dFdy would show; this puts it back.
		const char* const FRAGMENT_PRELUDE =
			"layout(constant_id = 0) const float loom_FlipY = 1.0;\n"
			"#define dFdy(p) (loom_FlipY * dFdy(p))\n"
			"#define dFdyFine(p) (loom_FlipY * dFdyFine(p))\n"
			"#define dFdyCoarse(p) (loom_FlipY * dFdyCoarse(p))\n";

#ifdef __EMSCRIPTEN__
		// WebGPU's clip space has y up only where its rows run top first, and
		// depth from 0, so the vertex stage ends by meeting it: y flipped for a
		// target, which keeps GL's rows bottom first, and z remapped.
		const char* const VERTEX_START = "#define main loom_clip_main\n";
		const char* const VERTEX_END =
			"\n#undef main\n"
			"layout(constant_id = 1) const float loom_ClipFlipY = 1.0;\n"
			"void main() { loom_clip_main(); gl_Position.y *= loom_ClipFlipY; gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5; }\n";
#else
		const char* const VERTEX_START = "";
		const char* const VERTEX_END = "";
#endif

		// glslang wants this once before anything else and once after.
		struct Process
		{
			Process() { glslang::InitializeProcess(); };
			~Process() { glslang::FinalizeProcess(); };
		};

		struct Stage
		{
			EShLanguage language;
			std::string source;
		};

		Program Build(const std::vector<Stage>& stages)
		{
			static const Process process;

			const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

			std::vector<std::unique_ptr<glslang::TShader>> shaders;
			glslang::TProgram linked;
			Program program;
			bool parsed = true;

			for (const Stage& stage : stages)
			{
				glslang::TShader& shader = *shaders.emplace_back(std::make_unique<glslang::TShader>(stage.language));
				const char* text = stage.source.c_str();

				shader.setStrings(&text, 1);
				shader.setEnvInput(glslang::EShSourceGlsl, stage.language, glslang::EShClientVulkan, VULKAN_INPUT_VERSION);
				shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
				shader.setEnvTarget(glslang::EShTargetSpv, SPIRV_VERSION);
				shader.setEnvInputVulkanRulesRelaxed();
				shader.setAutoMapBindings(true);
				shader.setAutoMapLocations(true);
				shader.setGlobalUniformSet(0);
				shader.setGlobalUniformBinding(DEFAULT_BLOCK_BINDING);

				const bool stageParsed = shader.parse(GetDefaultResources(), GLSL_VERSION, false, messages);

				program.log += shader.getInfoLog();
				parsed = parsed && stageParsed;

				linked.addShader(&shader);
			};

			if (!parsed)
				return program;

			if (!linked.link(messages) || !linked.mapIO())
			{
				program.log += linked.getInfoLog();
				return program;
			};

			for (const Stage& stage : stages)
				glslang::GlslangToSpv(*linked.getIntermediate(stage.language), program.spirv.emplace_back());

			program.compiled = true;

			return program;
		};

		uint32_t StageBit(SpvReflectShaderStageFlagBits stage)
		{
			switch (stage)
			{
			case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: return VERTEX_STAGE;
			case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: return FRAGMENT_STAGE;
			default: return COMPUTE_STAGE;
			};
		};

		std::string TypeName(const SpvReflectDescriptorBinding& binding)
		{
			if (binding.type_description && binding.type_description->type_name)
				return binding.type_description->type_name;

			return binding.name ? binding.name : "";
		};

		void ReflectStage(const std::string& name, const std::vector<uint32_t>& code, Layout& layout)
		{
			SpvReflectShaderModule module;

			if (spvReflectCreateShaderModule(code.size() * sizeof(uint32_t), code.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
				throw std::runtime_error("Could not reflect " + name);

			const std::unique_ptr<SpvReflectShaderModule, void(*)(SpvReflectShaderModule*)> destroy(&module, spvReflectDestroyShaderModule);

			const uint32_t stage = StageBit(module.shader_stage);
			const auto fail = [&](const std::string& what) { throw std::runtime_error("Shader error in " + name + ": " + what); };

			uint32_t count = 0;
			spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);

			std::vector<SpvReflectDescriptorBinding*> bindings(count);
			spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

			for (const SpvReflectDescriptorBinding* binding : bindings)
			{
				// One the stage never reads is left without a binding of its own, so
				// it can share one with what it does read.
				if (!binding->accessed)
					continue;

				const std::string resource = binding->name && *binding->name ? binding->name : TypeName(*binding);

				if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding->binding == DEFAULT_BLOCK_BINDING)
				{
					layout.blockSize = std::max(layout.blockSize, binding->block.padded_size);
					layout.blockStages |= stage;

					for (uint32_t i = 0; i < binding->block.member_count; i++)
					{
						const SpvReflectBlockVariable& member = binding->block.members[i];
						layout.members[member.name] = { member.offset, member.size, member.numeric.matrix.stride };
					};
				}
				else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				{
					Layout::Sampler& sampler = layout.samplers.try_emplace(binding->name, Layout::Sampler{ binding->binding, binding->image.depth == 1, 0 }).first->second;
					sampler.stages |= stage;
				}
				else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
				{
					const bool readonly = (binding->block.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;

					if (stage == VERTEX_STAGE && !readonly)
						fail("storage block " + resource + " must be readonly in a vertex stage");

					Layout::Binding& buffer = layout.buffers.try_emplace(TypeName(*binding), Layout::Binding{ binding->binding, 0, true }).first->second;
					buffer.stages |= stage;
					buffer.readonly = buffer.readonly && readonly;
				}
				else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE)
				{
					const bool writeonly = (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE) != 0;

					if (stage != COMPUTE_STAGE || !writeonly || binding->image.image_format != SpvImageFormatRgba8 || binding->image.dim != SpvDim2D || binding->image.arrayed)
						fail("image " + resource + " must be a writeonly rgba8 image2D in a compute stage");

					layout.images[binding->name] = { binding->binding, stage, false };
				}
				else
					fail(resource + " is a kind of uniform Loom does not set");
			};

			if (stage == VERTEX_STAGE)
			{
				spvReflectEnumerateInputVariables(&module, &count, nullptr);

				std::vector<SpvReflectInterfaceVariable*> inputs(count);
				spvReflectEnumerateInputVariables(&module, &count, inputs.data());

				for (const SpvReflectInterfaceVariable* variable : inputs)
					if (!(variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN))
						layout.inputs.push_back(variable->location);
			};

			if (stage == COMPUTE_STAGE)
			{
				const SpvReflectEntryPoint& entry = module.entry_points[0];
				layout.localSize = { entry.local_size.x, entry.local_size.y, entry.local_size.z };
			};
		};
	};

	Program Compile(const std::string& vertex, const std::string& fragment)
	{
		return Build(
		{
			{ EShLangVertex, PRELUDE + std::string(VERTEX_START) + LINE_RESET + vertex + VERTEX_END },
			{ EShLangFragment, PRELUDE + std::string(FRAGMENT_PRELUDE) + LINE_RESET + fragment },
		});
	};

	Program CompileCompute(const std::string& compute)
	{
		return Build({ { EShLangCompute, PRELUDE + std::string(LINE_RESET) + compute } });
	};

	Layout Reflect(const std::string& name, const Program& program)
	{
		Layout layout;

		for (const std::vector<uint32_t>& code : program.spirv)
			ReflectStage(name, code, layout);

		return layout;
	};
};

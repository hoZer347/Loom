#ifndef __EMSCRIPTEN__

#include "GlslCompiler.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>


namespace Loom::Glsl
{
	namespace
	{
		constexpr int GLSL_VERSION = 460;

		// The Vulkan GLSL semantics glslang compiles to (the VULKAN macro's value).
		constexpr int VULKAN_INPUT_VERSION = 100;

		const char* const VERSION_LINE = "#version 460 core\n";

		// The window is drawn with a flipped viewport, so its rows come out top
		// first while clip space keeps GL's y up. That reverses screen space y,
		// which dFdy would show; this puts it back. #line keeps the compiler's
		// line numbers where GL's would be.
		const char* const FRAGMENT_PRELUDE =
			"layout(constant_id = 0) const float loom_FlipY = 1.0;\n"
			"#define dFdy(p) (loom_FlipY * dFdy(p))\n"
			"#define dFdyFine(p) (loom_FlipY * dFdyFine(p))\n"
			"#define dFdyCoarse(p) (loom_FlipY * dFdyCoarse(p))\n"
			"#line 2\n";

		// glslang wants this once before anything else and once after.
		struct Process
		{
			Process() { glslang::InitializeProcess(); };
			~Process() { glslang::FinalizeProcess(); };
		};
	};

	Program Compile(const std::string& vertex, const std::string& fragment)
	{
		static const Process process;

		const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

		const std::string sources[STAGES] =
		{
			VERSION_LINE + vertex,
			VERSION_LINE + std::string(FRAGMENT_PRELUDE) + fragment,
		};

		const EShLanguage stages[STAGES] = { EShLangVertex, EShLangFragment };

		glslang::TShader vertexShader(EShLangVertex);
		glslang::TShader fragmentShader(EShLangFragment);
		glslang::TShader* shaders[STAGES] = { &vertexShader, &fragmentShader };

		glslang::TProgram linked;
		Program program;
		bool parsed = true;

		for (size_t i = 0; i < STAGES; i++)
		{
			glslang::TShader& shader = *shaders[i];
			const char* text = sources[i].c_str();

			shader.setStrings(&text, 1);
			shader.setEnvInput(glslang::EShSourceGlsl, stages[i], glslang::EShClientVulkan, VULKAN_INPUT_VERSION);
			shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
			shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
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

		for (size_t i = 0; i < STAGES; i++)
			glslang::GlslangToSpv(*linked.getIntermediate(stages[i]), program.spirv[i]);

		program.compiled = true;

		return program;
	};
};

#endif

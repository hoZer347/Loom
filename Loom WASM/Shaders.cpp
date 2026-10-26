#include "Shaders.h"

#include "GlslCompiler.h"
#include "Light.h"
#include "Renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <numeric>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#define SHADER_PATH ""


namespace Loom
{
	static inline std::string defaultShader;

	namespace
	{
		// The file is source string 0 to the compiler and the engine's GLSL is
		// 1, so nothing said about the engine's is taken for the file's.
		constexpr int FILE_SOURCE = 0;
		constexpr int LIBRARY_SOURCE = 1;

		// From GLSL 3.30 and ES 3.00 on, #line numbers the line after it.
		std::string LineDirective(size_t line, int source)
		{
			return "#line " + std::to_string(line) + ' ' + std::to_string(source) + '\n';
		};

		// Code the engine puts in a stage, numbered apart from the file's.
		std::string EngineCode(const std::string& code)
		{
			return code.empty() ? code : LineDirective(1, LIBRARY_SOURCE) + code;
		};

		// The vertex stage of a file without one: the mesh's positions, through
		// its transform and the camera.
		constexpr const char* DEFAULT_VERTEX = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		// The depth the vertex stage puts out is all the shadow map needs.
		constexpr const char* SHADOW_FRAGMENT = R"(
void main()
{
}
)";

		// A transparent shader's shadow pass keeps its alpha's share of each
		// 4x4 block of texels. The shadow map's filtering spans a block, so it
		// averages them back into a shadow as dark as the alpha.
		constexpr const char* DITHER_LIBRARY = R"(
const int LOOM_DITHER_SIZE = 4;
const float LOOM_DITHER_LEVELS = 16.0;
const float LOOM_DITHER_CENTRE = 0.5;
const float LOOM_DITHER[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

bool LoomDitheredOut(float alpha)
{
	ivec2 cell = ivec2(gl_FragCoord.xy) % LOOM_DITHER_SIZE;

	return alpha * LOOM_DITHER_LEVELS < LOOM_DITHER[cell.y * LOOM_DITHER_SIZE + cell.x] + LOOM_DITHER_CENTRE;
}
)";

		constexpr const char* SHADOW_PROGRAM_SUFFIX = " shadows";
		constexpr const char* INSTANCED_PROGRAM_SUFFIX = " instanced";
		constexpr const char* PIXEL_PASS_PROGRAM_SUFFIX = " pixel pass";

		// What the shading section's vertex half reads, and the declaration it
		// adds when the stage has not made its own.
		struct ShadingInput
		{
			const char* name;
			const char* declaration;
		};

		constexpr ShadingInput SHADING_INPUTS[] =
		{
			{ "aPos", "layout(location = 0) in vec3 aPos;" },
			{ "aNormal", "layout(location = 2) in vec3 aNormal;" },
			{ "u_model", "uniform mat4 u_model;" },
		};

		const std::regex& MainPattern()
		{
			// The whitespace is kept, so a main split over lines keeps the
			// lines after it where #line numbered them.
			static const std::regex pattern(R"(\b(void\s+)main(\s*\(\s*(?:void\s*)?\)))");

			return pattern;
		};

		// The fragment stage's colour, by name.
		const std::regex& ColorPattern()
		{
			static const std::regex pattern(R"(\bout\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+(\w+)\s*;)");

			return pattern;
		};

		bool Declares(const std::string& stage, const std::string& name)
		{
			const std::regex declaration(R"(\b(?:in|uniform)\s+(?:(?:lowp|mediump|highp)\s+)?\w+\s+)" + name + R"(\s*;)");

			return std::regex_search(stage, declaration);
		};

		// The stage's own main, renamed for the engine's to call before adding
		// its sections.
		std::string WithMainRenamed(const std::string& stage, const std::string& which)
		{
			if (!std::regex_search(stage, MainPattern()))
				throw std::runtime_error("Shading needs the " + which + " stage to have a main()");

			return std::regex_replace(stage, MainPattern(), "$1loom_main$2");
		};

		std::string ShadedVertex(const std::string& stage, bool shadows)
		{
			std::string shaded = WithMainRenamed(stage, "vertex") + '\n' + LineDirective(1, LIBRARY_SOURCE);

			for (const ShadingInput& input : SHADING_INPUTS)
				if (!Declares(stage, input.name))
					shaded += std::string(input.declaration) + '\n';

			shaded += "out vec3 loom_World;\nout vec3 loom_Normal;\n";

			if (shadows)
				shaded += "out vec4 loom_LightSpace;\n";

			// The model's inverse transpose keeps a normal square to its face
			// however the model is stretched. A mesh without normals gives
			// zeros, which stay zeros.
			shaded +=
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tvec4 loom_world = u_model * vec4(aPos, 1.0);\n"
				"\tloom_World = loom_world.xyz;\n"
				"\tloom_Normal = transpose(inverse(mat3(u_model))) * aNormal;\n";

			if (shadows)
				shaded += "\tloom_LightSpace = LoomLightSpace(loom_world, loom_Normal);\n";

			return shaded + "}\n";
		};

		std::string ShadedFragment(const std::string& stage, bool shadows)
		{
			std::smatch color;

			if (!std::regex_search(stage, color, ColorPattern()))
				throw std::runtime_error("Shading needs the fragment stage to write an out vec4 colour");

			std::string shaded = WithMainRenamed(stage, "fragment") + '\n' + LineDirective(1, LIBRARY_SOURCE) + "in vec3 loom_World;\nin vec3 loom_Normal;\n";

			if (shadows)
				shaded += "in vec4 loom_LightSpace;\n";

			// The face's own normal comes from the surface's slope across the
			// screen, and always faces the camera. A mesh with normals is lit by
			// them instead, smoothly across its faces, turned to the camera's
			// side so a sheet seen from behind is lit as its back. One without
			// them gives zeros, and is lit flat face by face.
			return shaded +
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tvec3 loom_face = normalize(cross(dFdx(loom_World), dFdy(loom_World)));\n"
				"\tvec3 loom_normal = dot(loom_Normal, loom_Normal) > 0.0 ? normalize(loom_Normal) : loom_face;\n"
				"\tloom_normal = dot(loom_normal, loom_face) < 0.0 ? -loom_normal : loom_normal;\n\n"
				"\t" + color[1].str() + ".rgb *= LoomLight(loom_normal" + (shadows ? ", loom_LightSpace" : "") + ");\n"
				"}\n";
		};

		std::string TransparentShadowFragment(const std::string& stage)
		{
			std::smatch color;

			if (!std::regex_search(stage, color, ColorPattern()))
				throw std::runtime_error("Transparency with shadows needs the fragment stage to write an out vec4 colour");

			// The pass has no colour attachment, so the colour becomes a plain
			// variable, losing any layout qualifier with its out.
			static const std::regex output(R"((?:\blayout\s*\([^)]*\)\s*)?\bout\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+(\w+)\s*;)");

			const std::string withoutOutput = std::regex_replace(WithMainRenamed(stage, "fragment"), output, "vec4 $1;");

			return withoutOutput + '\n' + EngineCode(DITHER_LIBRARY) +
				"\nvoid main()\n{\n"
				"\tloom_main();\n\n"
				"\tif (LoomDitheredOut(" + color[1].str() + ".a))\n"
				"\t\tdiscard;\n"
				"}\n";
		};

		// The stages of a program drawn as instances.
		struct InstancedStages
		{
			std::string vertex;
			std::string fragment;
		};

		// The stages, reading the model matrix, colour and seed each draw would
		// set from each instance, or none when the vertex stage declares no
		// model matrix, any is declared some other way, or the fragment stage
		// reads the model matrix.
		std::optional<InstancedStages> Instanced(const std::string& vertex, const std::string& fragment)
		{
			static const std::regex model(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?mat4\s+u_model\s*;)");
			static const std::regex color(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?vec3\s+u_color\s*;)");
			static const std::regex seed(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?float\s+u_seed\s*;)");
			static const std::regex otherwise(R"(\buniform\b[^;]*\bu_(?:model|color|seed)\b)");
			static const std::regex readsModel(R"(\bu_model\b)");

			if (!std::regex_search(vertex, model))
				return std::nullopt;

			const auto undeclared = [](const std::string& stage)
			{
				return std::regex_replace(std::regex_replace(stage, color, ""), seed, "");
			};

			InstancedStages stages{ undeclared(std::regex_replace(vertex, model, "")), undeclared(fragment) };

			if (std::regex_search(stages.vertex, otherwise) ||
				std::regex_search(stages.fragment, otherwise) ||
				std::regex_search(fragment, readsModel))
				return std::nullopt;

			// The colour and seed are the instance's, handed on to the fragment
			// stage unblended.
			stages.vertex =
				"layout(location = " + std::to_string(Renderer::InstanceModelLocation) + ") in mat4 loom_InstanceModel;\n"
				"layout(location = " + std::to_string(Renderer::InstanceColorLocation) + ") in vec4 loom_InstanceColorSeed;\n"
				"flat out vec3 loom_Color;\n"
				"flat out float loom_Seed;\n"
				"#define u_model loom_InstanceModel\n"
				"#define u_color loom_InstanceColorSeed.rgb\n"
				"#define u_seed loom_InstanceColorSeed.a\n" +
				std::regex_replace(stages.vertex, MainPattern(), "$1loom_instance_main$2") +
				"\nvoid main()\n{\n"
				"\tloom_Color = loom_InstanceColorSeed.rgb;\n"
				"\tloom_Seed = loom_InstanceColorSeed.a;\n"
				"\tloom_instance_main();\n"
				"}\n";

			stages.fragment = "flat in vec3 loom_Color;\nflat in float loom_Seed;\n#define u_color loom_Color\n#define u_seed loom_Seed\n" + stages.fragment;

			return stages;
		};

		// The program, or 0 when the stages do not compile.
		uint32_t CreateIfCompiles(Renderer& renderer, const std::string& name, const std::optional<InstancedStages>& stages)
		{
			if (!stages)
				return 0;

			try
			{
				return renderer.CreateProgram(name, stages->vertex, stages->fragment);
			}
			catch (const std::runtime_error&)
			{
				return 0;
			};
		};

		// A pixel pass is drawn as one quad over the view, which it reads from
		// texel by texel. The inverse of the camera's view is worked out here
		// rather than for each pixel.
		constexpr const char* PIXEL_PASS_VERTEX = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;

out vec2 loom_ScreenUV;
flat out mat4 loom_InverseViewProjection;

const float LOOM_UV_PER_NDC = 0.5;

void main()
{
	loom_ScreenUV = aPos.xy * LOOM_UV_PER_NDC + LOOM_UV_PER_NDC;
	loom_InverseViewProjection = inverse(u_viewProjection);
	gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)";

		// What every pixel pass reads the view through. Depth is 0 at the near
		// plane and 1 at the far, which is also where nothing was drawn.
		constexpr const char* PIXEL_PASS_LIBRARY = R"(
precision highp int;

uniform highp sampler2D u_scene;
uniform highp sampler2D u_sceneDepth;
uniform vec3 u_cameraPosition;

in vec2 loom_ScreenUV;
flat in mat4 loom_InverseViewProjection;

const float LOOM_UV_PER_NDC = 0.5;
const float LOOM_TEXEL_CENTRE = 0.5;
const float LOOM_FAR_DEPTH = 1.0;

ivec2 LoomScreenPixel()
{
	return ivec2(loom_ScreenUV * vec2(textureSize(u_scene, 0)));
}

ivec2 LoomClamped(ivec2 pixel)
{
	return clamp(pixel, ivec2(0), textureSize(u_scene, 0) - 1);
}

vec4 LoomScenePixel(ivec2 pixel)
{
	return texelFetch(u_scene, LoomClamped(pixel), 0);
}

float LoomSceneDepth(ivec2 pixel)
{
	return texelFetch(u_sceneDepth, LoomClamped(pixel), 0).r;
}

vec3 LoomScenePosition(ivec2 pixel)
{
	pixel = LoomClamped(pixel);

	vec2 uv = (vec2(pixel) + LOOM_TEXEL_CENTRE) / vec2(textureSize(u_sceneDepth, 0));
	vec4 ndc = vec4(uv / LOOM_UV_PER_NDC - 1.0, LoomSceneDepth(pixel) / LOOM_UV_PER_NDC - 1.0, 1.0);
	vec4 world = loom_InverseViewProjection * ndc;

	return world.xyz / world.w;
}

// The shorter of two steps to a neighbour, as the longer may cross an edge.
// A step of nothing is a neighbour clamped off the view.
vec3 LoomShorter(vec3 a, vec3 b)
{
	float lengthA = dot(a, a);
	float lengthB = dot(b, b);

	return lengthA > 0.0 && (lengthA < lengthB || lengthB == 0.0) ? a : b;
}

vec3 LoomSceneNormal(ivec2 pixel)
{
	if (LoomSceneDepth(pixel) >= LOOM_FAR_DEPTH)
		return vec3(0.0);

	vec3 centre = LoomScenePosition(pixel);
	vec3 across = LoomShorter(LoomScenePosition(pixel + ivec2(1, 0)) - centre, centre - LoomScenePosition(pixel - ivec2(1, 0)));
	vec3 up = LoomShorter(LoomScenePosition(pixel + ivec2(0, 1)) - centre, centre - LoomScenePosition(pixel - ivec2(0, 1)));
	vec3 normal = cross(across, up);

	if (dot(normal, normal) == 0.0)
		return vec3(0.0);

	normal = normalize(normal);

	return dot(normal, u_cameraPosition - centre) < 0.0 ? -normal : normal;
}
)";

		// What a pixel pass may declare that its library already has.
		constexpr const char* PIXEL_PASS_UNIFORMS[] = { "u_scene", "u_sceneDepth", "u_cameraPosition" };

		// What each kind of kernel holds of each pixel it covers, and what
		// reads it.
		struct KernelKind
		{
			std::string_view name;
			const char* type;
			const char* values;
			const char* read;
		};

		constexpr KernelKind KERNEL_KINDS[] =
		{
			{ "Pixel", "vec4", "pixels", "LoomScenePixel" },
			{ "Position", "vec3", "positions", "LoomScenePosition" },
			{ "Normal", "vec3", "normals", "LoomSceneNormal" },
		};

		// Past this a kernel reads more of the view for each pixel than a pass
		// should.
		constexpr int MAX_KERNEL_SIZE = 15;

		// Enough digits to keep a weight a float apart from the next.
		constexpr int WEIGHT_DIGITS = 9;

		const std::regex& KernelPattern()
		{
			static const std::regex pattern(R"(\b(Pixel|Position|Normal)Kernel_(\d+)x(\d+)\b)");

			return pattern;
		};

		constexpr size_t KERNEL_KIND_MATCH = 1;
		constexpr size_t KERNEL_WIDTH_MATCH = 2;
		constexpr size_t KERNEL_HEIGHT_MATCH = 3;

		// A kernel declared outside any function, by its type and name.
		const std::regex& GlobalKernelPattern()
		{
			static const std::regex pattern(R"(\b((?:Pixel|Position|Normal)Kernel_\d+x\d+)\s+(\w+)\s*;)");

			return pattern;
		};

		constexpr size_t GLOBAL_TYPE_MATCH = 1;
		constexpr size_t GLOBAL_NAME_MATCH = 2;

		std::string WithoutComments(const std::string& stage)
		{
			static const std::regex comment(R"(//[^\n]*|/\*[\s\S]*?\*/)");

			return std::regex_replace(stage, comment, " ");
		};

		// What is outside every brace, where a global is declared.
		std::string OutsideBraces(const std::string& stage)
		{
			std::string outside;
			int depth = 0;

			for (const char c : stage)
			{
				if (c == '{')
					depth++;
				else if (c == '}')
					depth = std::max(depth - 1, 0);
				else if (depth == 0)
					outside += c;
			};

			return outside;
		};

		// A GLSL float literal, which needs its point.
		std::string FloatLiteral(double value)
		{
			std::ostringstream literal;
			literal << std::setprecision(WEIGHT_DIGITS) << value;

			std::string text = literal.str();

			if (text.find_first_of(".e") == std::string::npos)
				text += ".0";

			return text;
		};

		// A row of Pascal's triangle over its sum: the binomial weights, the
		// discrete Gaussian of that width.
		std::vector<double> BinomialWeights(int size)
		{
			std::vector<double> weights(size, 1.0);

			for (int i = 1; i < size; i++)
				weights[i] = weights[i - 1] * (size - i) / i;

			const double sum = std::accumulate(weights.begin(), weights.end(), 0.0);

			for (double& weight : weights)
				weight /= sum;

			return weights;
		};

		std::string KernelSize(int width, int height)
		{
			return std::to_string(width) + 'x' + std::to_string(height);
		};

		std::string KernelWeights(int width, int height)
		{
			const std::vector<double> across = BinomialWeights(width);
			const std::vector<double> up = BinomialWeights(height);
			const std::string count = std::to_string(width * height);

			std::string weights = "\nconst float LOOM_WEIGHTS_" + KernelSize(width, height) + '[' + count + "] = float[" + count + "](";

			for (int y = 0; y < height; y++)
				for (int x = 0; x < width; x++)
					weights += (x || y ? ", " : "") + FloatLiteral(across[x] * up[y]);

			return weights + ");\n";
		};

		std::string Kernel(const KernelKind& kind, int width, int height)
		{
			const std::string size = KernelSize(width, height);
			const std::string name = std::string(kind.name) + "Kernel_" + size;
			const std::string count = std::to_string(width * height);

			return
				"\nstruct " + name + "\n{\n"
				"\t" + kind.type + ' ' + kind.values + '[' + count + "];\n"
				"\tivec2 offsets[" + count + "];\n"
				"\tfloat weights[" + count + "];\n"
				"\t" + kind.type + " centre;\n"
				"\tint count;\n"
				"};\n\n" +
				name + " Loom" + name + "(ivec2 pixel)\n{\n"
				"\t" + name + " kernel;\n"
				"\tkernel.count = " + count + ";\n\n"
				"\tfor (int i = 0; i < " + count + "; i++)\n\t{\n"
				"\t\tivec2 offset = ivec2(i % " + std::to_string(width) + " - " + std::to_string(width / 2) + ", i / " + std::to_string(width) + " - " + std::to_string(height / 2) + ");\n"
				"\t\tkernel.offsets[i] = offset;\n"
				"\t\tkernel.weights[i] = LOOM_WEIGHTS_" + size + "[i];\n"
				"\t\tkernel." + kind.values + "[i] = " + kind.read + "(pixel + offset);\n"
				"\t}\n\n"
				"\tkernel.centre = kernel." + kind.values + '[' + std::to_string(width * height / 2) + "];\n"
				"\treturn kernel;\n"
				"}\n";
		};

		// The library and each kernel the stage names, which go ahead of it.
		std::string PixelPassLibrary(const std::string& code)
		{
			std::string library = PIXEL_PASS_LIBRARY;
			std::vector<std::string> named;
			std::vector<std::string> sizes;

			for (std::sregex_iterator next(code.begin(), code.end(), KernelPattern()); next != std::sregex_iterator(); ++next)
			{
				const std::smatch& kernel = *next;

				if (std::ranges::find(named, kernel.str()) != named.end())
					continue;

				named.push_back(kernel.str());

				const int width = std::stoi(kernel[KERNEL_WIDTH_MATCH]);
				const int height = std::stoi(kernel[KERNEL_HEIGHT_MATCH]);

				if (width % 2 == 0 || height % 2 == 0 || width > MAX_KERNEL_SIZE || height > MAX_KERNEL_SIZE)
					throw std::runtime_error(
						kernel.str() + " has to be odd each way, so it has a centre, and at most " +
						std::to_string(MAX_KERNEL_SIZE) + " across");

				const std::string size = KernelSize(width, height);

				if (std::ranges::find(sizes, size) == sizes.end())
				{
					sizes.push_back(size);
					library += KernelWeights(width, height);
				};

				const KernelKind& kind = *std::ranges::find(KERNEL_KINDS, kernel[KERNEL_KIND_MATCH].str(), &KernelKind::name);
				library += Kernel(kind, width, height);
			};

			return library;
		};

		// The pass's fragment stage: the library ahead of its own code, and a
		// main that fills its global kernels in for the pixel being drawn
		// before running its own.
		std::string PixelPassFragment(const std::string& stage)
		{
			std::smatch color;

			if (!std::regex_search(stage, color, ColorPattern()))
				throw std::runtime_error("The pixel pass needs to write an out vec4 colour");

			const std::string code = WithoutComments(stage);
			const std::string outside = OutsideBraces(code);

			std::string filled;

			for (std::sregex_iterator next(outside.begin(), outside.end(), GlobalKernelPattern()); next != std::sregex_iterator(); ++next)
				filled += "\t" + (*next)[GLOBAL_NAME_MATCH].str() + " = Loom" + (*next)[GLOBAL_TYPE_MATCH].str() + "(loom_pixel);\n";

			// The library's functions read these ahead of where the stage
			// declares them.
			std::string own = WithMainRenamed(stage, "pixel pass");

			for (const char* uniform : PIXEL_PASS_UNIFORMS)
				own = std::regex_replace(own, std::regex(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?\w+\s+)" + std::string(uniform) + R"(\s*;)"), "");

			// What the pass draws is the view's final colour, which nothing is
			// seen through.
			return EngineCode(PixelPassLibrary(code)) + own + '\n' + LineDirective(1, LIBRARY_SOURCE) +
				"\nvoid main()\n{\n"
				"\tivec2 loom_pixel = LoomScreenPixel();\n" +
				filled +
				"\tloom_main();\n"
				"\t" + color[1].str() + ".a = 1.0;\n"
				"}\n";
		};

#ifndef __EMSCRIPTEN__
		// glslang's "ERROR: 0:12: 'x' : undeclared identifier", and its warnings.
		const std::regex& MessagePattern()
		{
			static const std::regex pattern(R"(^(ERROR|WARNING): (.*)$)");
			return pattern;
		};

		const std::regex& PlacePattern()
		{
			static const std::regex pattern(R"(^(\d+):(\d+): (.*)$)");
			return pattern;
		};

		// What glslang ends a stage that failed with.
		const std::regex& EndPattern()
		{
			static const std::regex pattern(R"(^\d+ compilation errors|: compilation terminated)");
			return pattern;
		};

		std::vector<ShaderMessage> ParseMessages(const std::string& log)
		{
			std::vector<ShaderMessage> messages;
			std::istringstream lines(log);

			for (std::string line; std::getline(lines, line);)
			{
				std::smatch match;

				if (!std::regex_match(line, match, MessagePattern()))
					continue;

				ShaderMessage message;
				message.error = match[1] == "ERROR";
				message.text = match[2];

				if (std::regex_search(message.text, EndPattern()))
					continue;

				std::smatch place;

				if (std::regex_match(message.text, place, PlacePattern()))
				{
					// A warning about the engine's GLSL is nothing the file can do
					// anything about.
					if (std::stoi(place[1]) != FILE_SOURCE && !message.error)
						continue;

					if (std::stoi(place[1]) == FILE_SOURCE)
						message.line = std::stoul(place[2]);

					message.text = place[3];
				};

				// COMMON is in both stages, and so is anything wrong with it.
				const bool repeated = std::any_of(
					messages.begin(),
					messages.end(),
					[&message](const ShaderMessage& other) { return other.line == message.line && other.text == message.text; });

				if (!repeated)
					messages.push_back(std::move(message));
			};

			return messages;
		};
#endif
	};

	Shader::Shader() :
		id(CompileSource(defaultShader))
	{ };

	Shader::Shader(const std::string& file_path) :
		file_path(file_path.ends_with(extension) ?
			file_path :
			file_path + extension),
		id(CompileSource(file_path))
	{ };

	Shader::Shader(const std::string& name, std::istream& source) :
		file_path(name),
		id(CompileStream(name, source))
	{ };

	Shader::~Shader()
	{
		std::scoped_lock lock{ mutex };

		shaders.erase(file_path);

		if (Renderer* renderer = Renderer::Get())
		{
			for (const uint32_t program : { id, shadow_id, instanced_id, shadow_instanced_id, pixel_pass_id })
				if (program)
					renderer->DeleteProgram(program);
		};
	};

	enum class ShaderType { NONE, COMMON, VERTEX, FRAGMENT, SETTINGS, PIXEL_PASS };

	uint32_t Shader::CompileSource(const std::string& file_path)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(file_path))
			return Cached(shaders[file_path]);

		std::istringstream file(ReadSource(file_path));

		return Build(file_path, file);
	};

	std::string Shader::ReadSource(const std::string& file_path)
	{
#if __EMSCRIPTEN__
		std::string source;
		Request(file_path, source);
		return source;
#else
		std::ifstream file(file_path);
		if (!file)
			throw std::runtime_error("could not open the file");

		std::error_code code;
		m_written = std::filesystem::last_write_time(file_path, code);

		return std::string(std::istreambuf_iterator<char>(file), { });
#endif
	};

	bool Shader::Reload()
	{
		std::scoped_lock lock{ mutex };

		const uint32_t previous[] = { id, shadow_id, instanced_id, shadow_instanced_id, pixel_pass_id };

		try
		{
			std::istringstream source(ReadSource(file_path));
			id = Build(file_path, source);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Shader: " << file_path << ": " << e.what() << std::endl;
			return false;
		};

		if (Renderer* renderer = Renderer::Get())
			for (const uint32_t program : previous)
				if (program)
					renderer->DeleteProgram(program);

		return true;
	};

	void Shader::ReloadIfChanged()
	{
		std::error_code code;
		const std::filesystem::file_time_type written = std::filesystem::last_write_time(file_path, code);

		if (!code && written != m_written)
			Reload();
	};

	uint32_t Shader::Cached(const Programs& programs)
	{
		settings = programs.settings;
		shadow_id = programs.shadow;
		instanced_id = programs.instanced;
		shadow_instanced_id = programs.shadowInstanced;
		pixel_pass_id = programs.pixelPass;
		vertex = programs.vertex;

		return programs.id;
	};

	uint32_t Shader::CompileStream(const std::string& name, std::istream& source)
	{
		std::scoped_lock lock{ mutex };

		if (shaders.contains(name))
			return Cached(shaders[name]);

		return Build(name, source);
	};

	namespace
	{
		// A scalar type a UV layer's field can be, the vectors of it, and how
		// one of the layer's words reads as it.
		struct UVScalar
		{
			std::string_view name;
			std::string_view vector;
			std::string_view before;
			std::string_view after;
		};

		constexpr UVScalar UV_SCALARS[] = {
			{ "float", "vec", "uintBitsToFloat(", ")" },
			{ "int", "ivec", "int(", ")" },
			{ "uint", "uvec", "", "" },
			{ "bool", "bvec", "(", " != 0u)" },
		};

		constexpr uint32_t MIN_VECTOR = 2;
		constexpr uint32_t MAX_VECTOR = 4;
		constexpr std::string_view SWIZZLE = "xyzw";

		static_assert(SWIZZLE.size() == Renderer::WordsPerUVLocation);

		// A vertex stage's UV_<id> declarations, each turned into the
		// attributes loom_UV_<id>_<slot> at the locations from
		// Renderer::UVLayerLocation on, and a UV_<id> that reads them as the
		// declared types, in a struct loom_UVLayer<id> when they are named.
		struct UVDeclarations
		{
			uint32_t nextLocation = Renderer::UVLayerLocation;
			std::unordered_set<uint32_t> declared;

			// UV_<id><type name, ...>; with a name for every type, read as a
			// struct, or UV_<id><type>; read as the type itself.
			static const std::regex& Pattern()
			{
				static const std::regex pattern(R"(^\s*UV_(\d+)\s*<([^>]*)>\s*;\s*(?://.*)?$)");
				return pattern;
			};

			// The GLSL for the declaration on the line, or nullopt when the
			// line is not one. Throws std::runtime_error for one that is
			// wrong.
			std::optional<std::string> Expand(const std::string& line, size_t number)
			{
				std::smatch match;

				if (!std::regex_match(line, match, Pattern()))
					return std::nullopt;

				const std::string where = "UV_" + match[1].str() + " on line " + std::to_string(number) + ": ";
				uint32_t id = 0;

				try
				{
					id = (uint32_t)std::stoul(match[1].str());
				}
				catch (const std::exception&)
				{
					throw std::runtime_error(where + "the id is too large");
				};

				if (!declared.insert(id).second)
					throw std::runtime_error(where + "declared twice");

				struct Field
				{
					std::string type;
					std::string name;
					const UVScalar* scalar = nullptr;
					uint32_t components = 1;
				};

				static const std::regex fieldPattern(R"(^\s*(\w+)(?:\s+(\w+))?\s*$)");
				std::vector<Field> fields;
				std::stringstream list(match[2].str());

				for (std::string text; std::getline(list, text, ',');)
				{
					std::smatch parts;

					if (!std::regex_match(text, parts, fieldPattern))
						throw std::runtime_error(where + "'" + text + "' is not a type and a name");

					Field field{ parts[1].str(), parts[2].str() };

					for (const UVScalar& scalar : UV_SCALARS)
					{
						if (field.type == scalar.name)
							field.scalar = &scalar;
						else if (field.type.size() == scalar.vector.size() + 1 && field.type.starts_with(scalar.vector))
						{
							const uint32_t components = (uint32_t)(field.type.back() - '0');

							if (components >= MIN_VECTOR && components <= MAX_VECTOR)
							{
								field.scalar = &scalar;
								field.components = components;
							};
						};
					};

					if (!field.scalar)
						throw std::runtime_error(where + "'" + field.type + "' is not a type a UV layer can hold");

					fields.push_back(field);
				};

				if (fields.empty())
					throw std::runtime_error(where + "no types");

				const bool named = !fields.front().name.empty();

				for (const Field& field : fields)
					if (field.name.empty() == named)
						throw std::runtime_error(where + "every type needs a name, or there is only one type and no name");

				if (!named && fields.size() > 1)
					throw std::runtime_error(where + "every type needs a name, or there is only one type and no name");

				uint32_t words = 0;

				for (const Field& field : fields)
					words += field.components;

				const uint32_t locations = (words + Renderer::WordsPerUVLocation - 1) / Renderer::WordsPerUVLocation;

				if (nextLocation + locations > Renderer::UVLayerLocation + Renderer::UVLayerLocations)
					throw std::runtime_error(
						where + "a vertex stage's UV layers hold at most " +
						std::to_string(Renderer::UVLayerLocations * Renderer::WordsPerUVLocation) + " words between them");

				const std::string attribute = "loom_UV_" + std::to_string(id);
				std::string code;

				for (uint32_t slot = 0; slot < locations; slot++)
					code += "layout(location = " + std::to_string(nextLocation + slot) + ") in uvec4 " + attribute + '_' + std::to_string(slot) + ";\n";

				nextLocation += locations;

				std::vector<std::string> reads;
				uint32_t word = 0;

				for (const Field& field : fields)
				{
					std::string read;

					for (uint32_t i = 0; i < field.components; i++, word++)
					{
						if (i)
							read += ", ";

						read += std::string(field.scalar->before) + attribute + '_' + std::to_string(word / Renderer::WordsPerUVLocation) + '.' +
							SWIZZLE[word % Renderer::WordsPerUVLocation] + std::string(field.scalar->after);
					};

					reads.push_back(field.components == 1 ? read : field.type + '(' + read + ')');
				};

				if (!named)
					return code + "#define UV_" + std::to_string(id) + ' ' + reads.front() + '\n';

				// Chrome's ANGLE on Direct3D fails to draw with a struct named
				// as the start of an attribute's name, so the struct has its own.
				const std::string type = "loom_UVLayer" + std::to_string(id);

				code += "struct " + type + " {";

				for (const Field& field : fields)
					code += ' ' + field.type + ' ' + field.name + ';';

				code += " };\n#define UV_" + std::to_string(id) + ' ' + type + '(';

				for (size_t i = 0; i < reads.size(); i++)
					code += (i ? ", " : "") + reads[i];

				return code + ")\n";
			};
		};
	};

	Shader::Stages Shader::Assemble(const std::string& source)
	{
		std::istringstream lines(source);

		std::unordered_map<ShaderType, std::stringstream> sections;
		ShaderType current = ShaderType::NONE;
		bool has_vertex = false;
		UVDeclarations uvDeclarations;

		size_t number = 0;

		// Each section starts on the file's own line number, so what the
		// compiler says about a line of a stage is about that line of the file.
		for (std::string line; std::getline(lines, line);)
		{
			number++;

			if (line.find(VERTEX_SECTION) != std::string::npos)
			{
				current = ShaderType::VERTEX;
				has_vertex = true;
			}
			else if (line.find(FRAGMENT_SECTION) != std::string::npos)
				current = ShaderType::FRAGMENT;
			else if (line.find(COMMON_SECTION) != std::string::npos)
				current = ShaderType::COMMON;
			else if (line.find(SETTINGS_SECTION) != std::string::npos)
				current = ShaderType::SETTINGS;
			else if (line.find(PIXEL_PASS_SECTION) != std::string::npos)
				current = ShaderType::PIXEL_PASS;
			else if (current == ShaderType::VERTEX)
			{
				const std::optional<std::string> expanded = uvDeclarations.Expand(line, number);

				if (!expanded)
				{
					sections[current] << line << '\n';
					continue;
				};

				sections[current] << *expanded;
			}
			else
			{
				if (current != ShaderType::NONE)
					sections[current] << line << '\n';

				continue;
			};

			sections[current] << LineDirective(number + 1, FILE_SOURCE);
		};

		Stages stages;
		stages.settings = ParseShaderSettings(source);

		const ShaderSettings& settings = stages.settings;
		const std::string common = sections[ShaderType::COMMON].str();
		const std::string vertex = common + (has_vertex ? sections[ShaderType::VERTEX].str() : EngineCode(DEFAULT_VERTEX));
		const std::string fragment = common + sections[ShaderType::FRAGMENT].str();

		stages.vertex = settings.shading ? ShadedVertex(vertex, settings.shadows) : vertex;
		stages.fragment = settings.shading ? ShadedFragment(fragment, settings.shadows) : fragment;

		if (settings.shadows)
		{
			stages.shadowVertex = vertex;
			stages.shadowFragment = settings.transparency ? TransparentShadowFragment(fragment) : common + EngineCode(SHADOW_FRAGMENT);
		};

		// The engine's GLSL goes first, so a shader's own COMMON block (its
		// precision statements included) has the last word.
		for (std::string* stage : { &stages.vertex, &stages.shadowVertex })
			if (!stage->empty())
				*stage = EngineCode(Light::VertexLibrary(*stage)) + *stage;

		for (std::string* stage : { &stages.fragment, &stages.shadowFragment })
			if (!stage->empty())
				*stage = EngineCode(Light::FragmentLibrary(*stage)) + *stage;

		if (sections.contains(ShaderType::PIXEL_PASS))
		{
			stages.pixelPassVertex = EngineCode(PIXEL_PASS_VERTEX);
			stages.pixelPass = PixelPassFragment(common + sections[ShaderType::PIXEL_PASS].str());
		};

		return stages;
	};

	uint32_t Shader::Build(const std::string& name, std::istream& source)
	{
		const std::string text(std::istreambuf_iterator<char>(source), { });

		Renderer* renderer = Renderer::Get();

		if (!renderer)
			throw std::runtime_error("No renderer to compile " + name + " with");

		const Stages stages = Assemble(text);

		Programs programs;
		programs.settings = stages.settings;
		programs.vertex = stages.vertex;
		programs.id = renderer->CreateProgram(name, stages.vertex, stages.fragment);
		renderer->SetPixelated(programs.id, stages.settings.pixelated);

		if (stages.settings.shadows)
			try
			{
				programs.shadow = renderer->CreateProgram(name + SHADOW_PROGRAM_SUFFIX, stages.shadowVertex, stages.shadowFragment);
			}
			catch (...)
			{
				renderer->DeleteProgram(programs.id);
				throw;
			};

		if (!stages.pixelPass.empty())
			try
			{
				programs.pixelPass = renderer->CreateProgram(name + PIXEL_PASS_PROGRAM_SUFFIX, stages.pixelPassVertex, stages.pixelPass);
			}
			catch (...)
			{
				for (const uint32_t program : { programs.id, programs.shadow })
					if (program)
						renderer->DeleteProgram(program);

				throw;
			};

		programs.instanced = CreateIfCompiles(*renderer, name + INSTANCED_PROGRAM_SUFFIX, Instanced(stages.vertex, stages.fragment));

		if (stages.settings.shadows)
			programs.shadowInstanced = CreateIfCompiles(*renderer, name + SHADOW_PROGRAM_SUFFIX + INSTANCED_PROGRAM_SUFFIX, Instanced(stages.shadowVertex, stages.shadowFragment));

		// After the programs, so a reload that fails leaves these describing
		// the ones still in use.
		variables = ParseShaderVariables(text);
		shaders[name] = programs;

		return Cached(programs);
	};

#ifndef __EMSCRIPTEN__
	std::vector<ShaderMessage> Shader::Check(const std::string& text)
	{
		Stages stages;

		try
		{
			stages = Assemble(text);
		}
		catch (const std::runtime_error& e)
		{
			return { ShaderMessage{ 0, true, e.what() } };
		};

		std::string log = Glsl::Compile(stages.vertex, stages.fragment).log;

		if (stages.settings.shadows)
			log += Glsl::Compile(stages.shadowVertex, stages.shadowFragment).log;

		if (!stages.pixelPass.empty())
			log += Glsl::Compile(stages.pixelPassVertex, stages.pixelPass).log;

		return ParseMessages(log);
	};
#endif

	bool Shader::AnyPixelPass()
	{
		std::scoped_lock lock{ mutex };

		return std::ranges::any_of(shaders, [](const auto& shader) { return shader.second.pixelPass != 0; });
	};

#ifdef __EMSCRIPTEN__
	void Shader::Request(
		const std::string& file_path,
		std::string& shader_source)
	{
		bool fetchComplete = false;
		bool fetchSuccess = false;

		emscripten_fetch_attr_t attr;
		emscripten_fetch_attr_init(&attr);
		strcpy(attr.requestMethod, "GET");
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;

		// Success callback
		attr.onsuccess = [](emscripten_fetch_t* fetch)
		{
			auto* fetchData = reinterpret_cast<std::pair<std::string*, bool*>*>(fetch->userData);
			std::string* target = fetchData->first;
			*target = std::string(fetch->data, fetch->numBytes);
			*fetchData->second = true; // Mark fetch as successful
			emscripten_fetch_close(fetch);
			delete fetchData;
		};

		// Error callback
		attr.onerror = [](emscripten_fetch_t* fetch)
		{
			auto* fetchData = reinterpret_cast<std::pair<std::string*, bool*>*>(fetch->userData);
			*fetchData->second = true; // Mark fetch as complete (even on error)
			emscripten_fetch_close(fetch);
			delete fetchData;
		};

		// Pass the shader_source and fetchComplete flag as user data
		attr.userData = new std::pair<std::string*, bool*>(&shader_source, &fetchComplete);
		emscripten_fetch(&attr, file_path.c_str());

		// Busy-wait until fetch is complete
		while (!fetchComplete)
			emscripten_sleep(10); // Yield control to the browser (non-blocking wait)

		// Thrown like a file that will not open, so a Material or a reload
		// reports it once, with the path.
		if (shader_source.empty())
			throw std::runtime_error("fetched nothing");
	};
#endif
};

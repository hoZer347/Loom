#include "doctest.h"

#include "Shaders.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>


namespace
{
	const std::string FRAGMENT =
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0); }\n";

	const std::string VERTEX =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_model;\n"
		"void main() { gl_Position = u_model * vec4(aPos, 1.0); }\n";

	size_t Count(const std::string& text, const std::string& part)
	{
		size_t count = 0;

		for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
			count++;

		return count;
	};
};


TEST_SUITE("Shader")
{
	// Shader compiles on construction, so most of it needs a GL context. What
	// does not is the source-loading step in front of the compiler, which is
	// where a wrong path or a malformed shader file gets caught.

	TEST_CASE("the shader registry starts out without a bogus path in it")
	{
		CHECK_FALSE(Loom::Shader::shaders.contains("no such shader.shader"));
	};

#ifndef __EMSCRIPTEN__
	// The native build reads shader sources off disk with an ifstream, and
	// fails before it ever reaches glCreateProgram -- so this is reachable with
	// no window open. The web build fetches over HTTP instead, which needs a
	// browser, so the same case is skipped there.
	TEST_CASE("constructing a Shader from a missing file throws")
	{
		CHECK_THROWS_AS(
			Loom::Shader("this file does not exist.shader"),
			std::runtime_error);
	};

	TEST_CASE("a failed compile does not leave the path in the registry")
	{
		const std::string missing = "another missing shader.shader";

		try { Loom::Shader shader(missing); }
		catch (const std::runtime_error&) { };

		CHECK_FALSE(Loom::Shader::shaders.contains(missing));
	};

	// The one-argument constructor appends the extension for you, so callers can
	// say Shader("Loom") instead of Shader("Loom.shader").
	//
	// It only half works today: file_path is normalised, but the id is compiled
	// from the raw constructor argument rather than from the normalised member,
	// so "Loom" and "Loom.shader" are looked up as two different shaders. Both
	// paths fail the same way here, which is all this can check without a
	// context.
	TEST_CASE("both spellings of a shader path fail the same way when the file is missing")
	{
		CHECK_THROWS_AS(Loom::Shader("missing"), std::runtime_error);
		CHECK_THROWS_AS(Loom::Shader("missing.shader"), std::runtime_error);
	};

	TEST_CASE("Check has nothing to say about a shader that compiles")
	{
		const std::string text =
			"// ===VERTEX===\n"
			"layout(location = 0) in vec3 aPos;\n"
			"uniform mat4 u_viewProjection;\n"
			"uniform mat4 u_model;\n"
			"out vec4 v_lightSpace;\n"
			"void main()\n"
			"{\n"
			"\tvec4 world = u_model * vec4(aPos, 1.0);\n"
			"\tv_lightSpace = LoomLightSpace(world);\n"
			"\tgl_Position = u_viewProjection * world;\n"
			"}\n"
			"// ===FRAGMENT===\n"
			"in vec4 v_lightSpace;\n"
			"out vec4 FragColor;\n"
			"void main()\n"
			"{\n"
			"\tFragColor = vec4(LoomLight(vec3(0.0, 1.0, 0.0), v_lightSpace), 1.0);\n"
			"}\n";

		CHECK(Loom::Shader::Check(text).empty());
	};

	TEST_CASE("Check puts an error on the line of the file it is on")
	{
		const std::string text =
			"// A comment above the sections.\n"
			"// ===VERTEX===\n"
			"layout(location = 0) in vec3 aPos;\n"
			"void main() { gl_Position = vec4(aPos, 1.0); }\n"
			"// ===FRAGMENT===\n"
			"out vec4 FragColor;\n"
			"void main()\n"
			"{\n"
			"\tFragColor = undeclaredColour;\n"
			"}\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].error);
		CHECK(messages[0].text.find("undeclaredColour") != std::string::npos);

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 9);
	};

	TEST_CASE("Check reports a mistake in COMMON once, though both stages have it")
	{
		const std::string text =
			"// ===COMMON===\n"
			"const float scale = undeclaredScale;\n"
			"// ===VERTEX===\n"
			"void main() { gl_Position = vec4(scale); }\n"
			"// ===FRAGMENT===\n"
			"out vec4 FragColor;\n"
			"void main() { FragColor = vec4(scale); }\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		const auto undeclared = [](const Loom::ShaderMessage& message) { return message.text.find("undeclaredScale") != std::string::npos; };

		CHECK(std::count_if(messages.begin(), messages.end(), undeclared) == 1);

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 2);
	};

	TEST_CASE("Check reports a stage with no main against no line")
	{
		const std::string text =
			"// ===VERTEX===\n"
			"void main() { gl_Position = vec4(0.0); }\n"
			"// ===FRAGMENT===\n"
			"out vec4 FragColor;\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].error);
		CHECK(messages[0].line == 0);
	};

	TEST_CASE("Check compiles a shaded shader with its shadow pass as the engine would, settings left out")
	{
		CHECK(Loom::Shader::Check("// ===SETTINGS===\nshading = on\nshadows = on\n" + VERTEX + FRAGMENT).empty());
	};

	TEST_CASE("Check puts a mistake in a shaded stage on the file's line, past the code shading adds")
	{
		const std::string text =
			"// ===SETTINGS===\n"
			"shading = on\n"
			"// ===FRAGMENT===\n"
			"out vec4 FragColor;\n"
			"void main() { FragColor = vec4(missingTint); }\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].text.find("missingTint") != std::string::npos);

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 5);
	};

	// Renaming a main written over two lines keeps both, or every line after
	// it would be numbered one short.
	TEST_CASE("Check numbers the lines after a main split over two as the file does")
	{
		const std::string text =
			"// ===SETTINGS===\n"
			"shading = on\n"
			"// ===FRAGMENT===\n"
			"out vec4 FragColor;\n"
			"void\n"
			"main()\n"
			"{\n"
			"\tFragColor = vec4(missingTint);\n"
			"}\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		REQUIRE_FALSE(messages.empty());

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 8);
	};

	TEST_CASE("Check reports a setting it does not know against no line")
	{
		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check("// ===SETTINGS===\nglow = on\n" + FRAGMENT);

		REQUIRE(messages.size() == 1);
		CHECK(messages[0].error);
		CHECK(messages[0].line == 0);
	};
#endif

	TEST_CASE("Shader is a plain resource handle, not a component")
	{
		// Materials hold a Shader*; a shader is not attached to a GameObject and
		// has no lifecycle callbacks of its own.
		CHECK(std::is_final_v<Loom::Shader>);

		// Its const members make it non-assignable, but the compiler still
		// generates a copy constructor -- and ~Shader calls glDeleteShader, so a
		// copy would delete the same GL program twice. Shaders are meant to be
		// held by pointer; this records that copying one is not defended against.
		CHECK(std::is_copy_constructible_v<Loom::Shader>);
		CHECK_FALSE(std::is_copy_assignable_v<Loom::Shader>);
	};

	TEST_CASE("a file with only a fragment stage is given the engine's vertex stage, and nothing else")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(FRAGMENT);

		CHECK(stages.vertex.find("gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);") != std::string::npos);
		CHECK(stages.fragment == "#line 2 0\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n");
		CHECK(stages.shadowVertex.empty());
		CHECK(stages.shadowFragment.empty());
		CHECK_FALSE(stages.settings.shading);
		CHECK_FALSE(stages.settings.shadows);
	};

	TEST_CASE("with its settings off, a file's stages compile as written")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(
			"// ===SETTINGS===\n"
			"// Nothing on yet.\n"
			"shading = off\n"
			"\n" +
			VERTEX + FRAGMENT);

		CHECK(stages.vertex == "#line 6 0\nlayout(location = 0) in vec3 aPos;\nuniform mat4 u_model;\nvoid main() { gl_Position = u_model * vec4(aPos, 1.0); }\n");
		CHECK(stages.fragment == "#line 10 0\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n");
	};

	TEST_CASE("shading wraps each stage's main and lights the colour the fragment stage writes")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\n" + VERTEX + FRAGMENT);

		CHECK(stages.settings.shading);

		// The stage's own main still runs, first.
		CHECK(stages.vertex.find("void loom_main() { gl_Position") != std::string::npos);
		CHECK(stages.vertex.find("out vec3 loom_World;") != std::string::npos);
		CHECK(stages.fragment.find("void loom_main() { FragColor") != std::string::npos);
		CHECK(stages.fragment.find("FragColor.rgb *= LoomLight(loom_normal);") != std::string::npos);

		// Lit by the mesh's normals when it has them, by each face's own when not.
		CHECK(stages.vertex.find("loom_Normal = transpose(inverse(mat3(u_model))) * aNormal;") != std::string::npos);
		CHECK(stages.fragment.find("dot(loom_Normal, loom_Normal) > 0.0 ? normalize(loom_Normal) : loom_face") != std::string::npos);

		// Calling LoomLight brings the light's GLSL in.
		CHECK(stages.fragment.find("uniform vec3 u_lightDirection;") != std::string::npos);

		// What the stage declares itself is not declared again.
		CHECK(Count(stages.vertex, "in vec3 aPos;") == 1);
		CHECK(Count(stages.vertex, "uniform mat4 u_model;") == 1);

		CHECK(stages.vertex.find("loom_LightSpace") == std::string::npos);
		CHECK(stages.shadowVertex.empty());
	};

	TEST_CASE("shading a vertex stage that never declared its inputs declares them")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(
			"// ===SETTINGS===\nshading = on\n"
			"// ===VERTEX===\nvoid main() { gl_Position = vec4(0.0); }\n" +
			FRAGMENT);

		CHECK(Count(stages.vertex, "layout(location = 0) in vec3 aPos;") == 1);
		CHECK(Count(stages.vertex, "layout(location = 2) in vec3 aNormal;") == 1);
		CHECK(Count(stages.vertex, "uniform mat4 u_model;") == 1);
	};

	TEST_CASE("shadows add a shadow pass made of the vertex stage, and shade by the shadow map")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\nshadows = on\n" + FRAGMENT);

		CHECK(stages.vertex.find("loom_LightSpace = LoomLightSpace(loom_world, loom_Normal);") != std::string::npos);
		CHECK(stages.vertex.find("uniform mat4 u_lightViewProjection;") != std::string::npos);
		CHECK(stages.fragment.find("FragColor.rgb *= LoomLight(loom_normal, loom_LightSpace);") != std::string::npos);

		// The pass is the vertex stage as it was before shading wrapped it.
		CHECK(stages.shadowVertex.find("void main()\n{\n\tgl_Position = u_viewProjection * u_model") != std::string::npos);
		CHECK(stages.shadowVertex.find("loom_") == std::string::npos);
		CHECK(stages.shadowFragment.find("void main()") != std::string::npos);
	};

	TEST_CASE("a transparent shader's shadow pass runs its fragment stage and dithers out by its alpha")
	{
		const std::string source = "// ===SETTINGS===\nshading = on\nshadows = on\ntransparency = on\n" + FRAGMENT;
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(source);

		CHECK(stages.shadowFragment.find("FragColor = vec4(1.0);") != std::string::npos);
		CHECK(stages.shadowFragment.find("out vec4 FragColor;") == std::string::npos);
		CHECK(stages.shadowFragment.find("if (LoomDitheredOut(FragColor.a))") != std::string::npos);
#ifndef __EMSCRIPTEN__
		CHECK(Loom::Shader::Check(source).empty());
#endif
	};

	TEST_CASE("a transparent shader's shadow pass drops the layout qualifier along with the out")
	{
		const std::string source =
			"// ===SETTINGS===\nshadows = on\ntransparency = on\n"
			"// ===FRAGMENT===\n"
			"layout(location = 0) out vec4 FragColor;\n"
			"void main() { FragColor = vec4(1.0); }\n";

		CHECK(Loom::Shader::Assemble(source).shadowFragment.find("layout(location = 0)") == std::string::npos);
#ifndef __EMSCRIPTEN__
		CHECK(Loom::Shader::Check(source).empty());
#endif
	};

	TEST_CASE("shadows without shading cast them, and leave the colour as written")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble("// ===SETTINGS===\nshadows = on\n" + FRAGMENT);

		CHECK(stages.fragment == "#line 4 0\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n");
		CHECK_FALSE(stages.shadowVertex.empty());
	};

	TEST_CASE("shading a stage it has nothing to add to is refused")
	{
		// No colour to light.
		CHECK_THROWS_AS(
			Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\n// ===FRAGMENT===\nvoid main() { }\n"),
			std::runtime_error);

		// No main to call.
		CHECK_THROWS_AS(
			Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\n// ===VERTEX===\nfloat Helper() { return 1.0; }\n" + FRAGMENT),
			std::runtime_error);
	};

	TEST_CASE("a settings line that is not a setting is refused")
	{
		for (const char* line : { "bloom = on\n", "shading = maybe\n", "shading on\n" })
			CHECK_THROWS_AS(Loom::Shader::Assemble(std::string("// ===SETTINGS===\n") + line + FRAGMENT), std::runtime_error);

		// The same words outside the section are the stages' business.
		CHECK_NOTHROW(Loom::Shader::Assemble("bloom = on\n" + FRAGMENT));
	};

	TEST_CASE("a file with no pixel pass section has no pixel pass")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(VERTEX + FRAGMENT);

		CHECK(stages.pixelPass.empty());
		CHECK(stages.pixelPassVertex.empty());
	};

	TEST_CASE("a pixel pass gets each kernel it names once, and its global kernels filled in for the pixel before its main runs")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(
			FRAGMENT +
			"// ===PIXEL PASS===\n"
			"out vec4 PassColour;\n"
			"PixelKernel_3x3 around;\n"
			"void main()\n"
			"{\n"
			"\tNormalKernel_3x3 normals = LoomNormalKernel_3x3(LoomScreenPixel());\n"
			"\tPixelKernel_3x3 again = LoomPixelKernel_3x3(LoomScreenPixel());\n"
			"\tPassColour = around.centre + again.centre + vec4(normals.centre, 0.0);\n"
			"}\n");

		CHECK(Count(stages.pixelPass, "struct PixelKernel_3x3") == 1);
		CHECK(Count(stages.pixelPass, "struct NormalKernel_3x3") == 1);
		CHECK(Count(stages.pixelPass, "struct PositionKernel") == 0);
		CHECK(Count(stages.pixelPass, "const float LOOM_WEIGHTS_3x3[9]") == 1);
		CHECK(Count(stages.pixelPass, "around = LoomPixelKernel_3x3(loom_pixel);") == 1);
		CHECK(Count(stages.pixelPass, "again = LoomPixelKernel_3x3(loom_pixel);") == 0);
		CHECK(stages.pixelPass.find("around = LoomPixelKernel_3x3(loom_pixel);") < stages.pixelPass.find("\tloom_main();"));
		CHECK(stages.pixelPass.find("PassColour.a = 1.0;") > stages.pixelPass.find("\tloom_main();"));
		CHECK(stages.pixelPassVertex.find("loom_InverseViewProjection") != std::string::npos);
	};

	TEST_CASE("a kernel's weights are the binomial ones, which sum to one, about its centre")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(
			"// ===PIXEL PASS===\n"
			"out vec4 PassColour;\n"
			"PixelKernel_3x1 around;\n"
			"void main() { PassColour = around.centre; }\n");

		CHECK(stages.pixelPass.find("float[3](0.25, 0.5, 0.25)") != std::string::npos);
		CHECK(stages.pixelPass.find("ivec2(i % 3 - 1, i / 3 - 0)") != std::string::npos);
	};

	TEST_CASE("a kernel's size run straight onto its kind declares no kernel")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(
			"// ===PIXEL PASS===\n"
			"out vec4 PassColour;\n"
			"PixelKernel3x3 around;\n"
			"void main() { PassColour = vec4(1.0); }\n");

		CHECK(Count(stages.pixelPass, "struct PixelKernel") == 0);
	};

	TEST_CASE("a kernel with no centre, or too big, is refused")
	{
		for (const char* kernel : { "PixelKernel_4x4", "NormalKernel_3x2", "PositionKernel_17x17" })
			CHECK_THROWS_AS(
				Loom::Shader::Assemble(
					std::string("// ===PIXEL PASS===\nout vec4 PassColour;\n") + kernel +
					" around;\nvoid main() { PassColour = vec4(1.0); }\n"),
				std::runtime_error);
	};

	TEST_CASE("a pixel pass that writes no colour is refused")
	{
		CHECK_THROWS_AS(
			Loom::Shader::Assemble("// ===PIXEL PASS===\nvoid main() { }\n"),
			std::runtime_error);
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE("Check compiles a pixel pass with every kind of kernel, and the scene's own uniforms declared again")
	{
		const std::string text =
			"// ===COMMON===\n"
			"float Strength() { return 0.5; }\n" +
			FRAGMENT +
			"// ===PIXEL PASS===\n"
			"uniform sampler2D u_scene;\n"
			"out vec4 PassColour;\n"
			"PixelKernel_5x5 around;\n"
			"// PositionKernel_4x4 in a comment is no kernel.\n"
			"void main()\n"
			"{\n"
			"\tNormalKernel_3x3 normals = LoomNormalKernel_3x3(LoomScreenPixel());\n"
			"\tPositionKernel_3x3 positions = LoomPositionKernel_3x3(LoomScreenPixel());\n"
			"\tvec4 blurred = vec4(0.0);\n"
			"\tfor (int i = 0; i < around.count; i++)\n"
			"\t\tblurred += around.pixels[i] * around.weights[i];\n"
			"\tfloat edge = 1.0 - dot(normals.centre, normals.normals[0]);\n"
			"\tPassColour = blurred * edge * Strength() + vec4(positions.centre * LoomSceneDepth(LoomScreenPixel()), 0.0);\n"
			"}\n";

		CHECK(Loom::Shader::Check(text).empty());
	};

	TEST_CASE("Check puts a mistake in a pixel pass on the file's line, past the code the pass adds")
	{
		const std::string text =
			FRAGMENT +
			"// ===PIXEL PASS===\n"
			"out vec4 PassColour;\n"
			"PixelKernel_3x3 around;\n"
			"void main()\n"
			"{\n"
			"\tPassColour = undeclaredColour;\n"
			"}\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::Check(text);

		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].text.find("undeclaredColour") != std::string::npos);

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 9);
	};
#endif

	TEST_CASE("a vertex stage's UV layer reads its fields from raw words, past four of them in a second attribute")
	{
		const std::string source =
			"// ===VERTEX===\n"
			"layout(location = 0) in vec3 aPos;\n"
			"UV_1<float weight, bool lit, ivec3 cell>;\n"
			"UV_4<vec2>;\n"
			"out vec4 v_color;\n"
			"void main() { v_color = vec4(UV_1.weight, UV_1.lit ? 1.0 : 0.0, float(UV_1.cell.z), UV_4.y); gl_Position = vec4(aPos, 1.0); }\n" +
			FRAGMENT;

		const Loom::Shader::Stages stages = Loom::Shader::Assemble(source);

		CHECK(stages.vertex.find("layout(location = 4) in uvec4 loom_UV_1_0;\n") != std::string::npos);
		CHECK(stages.vertex.find("layout(location = 5) in uvec4 loom_UV_1_1;\n") != std::string::npos);
		CHECK(stages.vertex.find("layout(location = 6) in uvec4 loom_UV_4_0;\n") != std::string::npos);
		CHECK(stages.vertex.find("struct loom_UV_1 { float weight; bool lit; ivec3 cell; };") != std::string::npos);
		CHECK(stages.vertex.find(
			"#define UV_1 loom_UV_1(uintBitsToFloat(loom_UV_1_0.x), (loom_UV_1_0.y != 0u), "
			"ivec3(int(loom_UV_1_0.z), int(loom_UV_1_0.w), int(loom_UV_1_1.x)))\n") != std::string::npos);
		CHECK(stages.vertex.find("#define UV_4 vec2(uintBitsToFloat(loom_UV_4_0.x), uintBitsToFloat(loom_UV_4_0.y))\n") != std::string::npos);

		// The lines after a declaration keep their numbers in the file.
		CHECK(stages.vertex.find("#line 4 0\nlayout(location = 6)") != std::string::npos);
		CHECK(stages.vertex.find("#line 5 0\nout vec4 v_color;") != std::string::npos);
#ifndef __EMSCRIPTEN__
		CHECK(Loom::Shader::Check(source).empty());
#endif
	};

	TEST_CASE("a UV layer declared wrongly is refused")
	{
		const std::string wrong[] = {
			"UV_1<double weight>;\n",
			"UV_1<vec5>;\n",
			"UV_1<float weight, vec3>;\n",
			"UV_1<float, vec3>;\n",
			"UV_1<>;\n",
			"UV_1<float>;\nUV_1<int>;\n",
			"UV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4 a, vec4 b, vec4 c, float d>;\n",
		};

		for (const std::string& declared : wrong)
			CHECK_THROWS_AS(Loom::Shader::Assemble("// ===VERTEX===\n" + declared + "void main() { }\n" + FRAGMENT), std::runtime_error);

		// Seven attributes' worth is as many as a stage reads.
		CHECK_NOTHROW(Loom::Shader::Assemble("// ===VERTEX===\nUV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4 a, vec4 b, vec4 c>;\nvoid main() { }\n" + FRAGMENT));

		// A fragment stage's UV_ is its own business.
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(VERTEX + "// ===FRAGMENT===\nUV_1<double weight>;\nvoid main() { }\n");

		CHECK(stages.fragment.find("UV_1<double weight>;") != std::string::npos);
	};
};

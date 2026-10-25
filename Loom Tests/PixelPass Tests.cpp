#include "doctest.h"

#include "Test Support.h"

#include "EditorViewport.h"
#include "Material.h"
#include "PixelPass.h"
#include "Shaders.h"

#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


namespace
{
	const glm::ivec2 VIEW(64, 32);

	const std::string STAGES =
		"// ===VERTEX===\n"
		"void main() { }\n"
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0); }\n";

	const std::string PASS =
		"// ===PIXEL PASS===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = LoomScenePixel(LoomScreenPixel()); }\n";

	// A shader with a pixel pass, and a material drawn with it.
	struct Passed
	{
		explicit Passed(const std::string& name) :
			source(STAGES + PASS),
			shader(name, source)
		{
			material.shader = &shader;
			Pump();
		};

		std::istringstream source;
		Loom::Shader shader;
		Loom::Material material;
	};
};


TEST_SUITE("PixelPass")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader with a pixel pass section builds a program for it, and one without does not")
	{
		RecordingRenderer renderer;

		Passed passed("passed");

		std::istringstream plain_source(STAGES);
		Loom::Shader plain("plain", plain_source);

		CHECK(passed.shader.pixel_pass_id != 0);
		CHECK(renderer.programs.at("passed pixel pass") == passed.shader.pixel_pass_id);
		CHECK(plain.pixel_pass_id == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a view drawn with no pixel pass is copied as it is")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;

		std::istringstream plain_source(STAGES);
		Loom::Shader plain("plain", plain_source);
		Loom::Material material;
		material.shader = &plain;
		Pump();

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(material);
		CHECK_FALSE(pass.End());

		pass.Draw(view, VIEW);

		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawnInto[0] == RecordingRenderer::WINDOW);
		CHECK(renderer.drawnScenes[0] == view);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a pass reads the view and its depth, once however many of its materials were drawn")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed passed("passed");

		Loom::Material other;
		other.shader = &passed.shader;
		Pump();

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(passed.material);
		Loom::PixelPass::Note(other);
		CHECK(pass.End());

		pass.Draw(view, VIEW);

		CHECK(renderer.drawn == std::vector<uint32_t>{ passed.shader.pixel_pass_id });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ RecordingRenderer::WINDOW });
		CHECK(renderer.drawnScenes == std::vector<uint32_t>{ view });
		CHECK(renderer.textures.at("u_sceneDepth") == renderer.TargetDepth(view));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "passes run in the order their materials were drawn, each reading what the one before it left")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed first("first");
		Passed second("second");
		Passed third("third");

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(second.material);
		Loom::PixelPass::Note(first.material);
		Loom::PixelPass::Note(third.material);
		pass.End();

		pass.Draw(view, VIEW);

		const std::vector<uint32_t> order{ second.shader.pixel_pass_id, first.shader.pixel_pass_id, third.shader.pixel_pass_id };
		REQUIRE(renderer.drawn == order);

		const uint32_t into_first = renderer.drawnInto[0];
		const uint32_t into_second = renderer.drawnInto[1];

		CHECK(into_first != RecordingRenderer::WINDOW);
		CHECK(into_second != RecordingRenderer::WINDOW);
		CHECK(into_first != into_second);
		CHECK(renderer.targetSizes.at(into_first) == VIEW);
		CHECK(renderer.drawnInto[2] == RecordingRenderer::WINDOW);
		CHECK(renderer.drawnScenes == std::vector<uint32_t>{ view, into_first, into_second });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material drawn while nothing is noting is not noted")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed passed("passed");

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		CHECK_FALSE(pass.End());

		Loom::PixelPass::Note(passed.material);
		pass.Draw(view, VIEW);

		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawn[0] != passed.shader.pixel_pass_id);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the window takes the pixel pass path only while a shader with one is loaded")
	{
		RecordingRenderer renderer;

		CHECK_FALSE(Loom::Shader::AnyPixelPass());

		{
			Passed passed("passed");
			CHECK(Loom::Shader::AnyPixelPass());
		};

		CHECK_FALSE(Loom::Shader::AnyPixelPass());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a pass begun inside another notes in its place, then hands noting back")
	{
		RecordingRenderer renderer;
		Loom::PixelPass outer;
		Loom::PixelPass inner;
		Passed passed("passed");

		outer.Begin();
		inner.Begin();
		inner.End();
		Loom::PixelPass::Note(passed.material);

		CHECK(outer.End());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a viewport drawn with a pixel pass shows the pass, with what goes over it drawn after it")
	{
		RecordingRenderer renderer;
		Passed passed("passed");
		Loom::EditorViewport viewport;

		viewport.Render(
			VIEW.x,
			VIEW.y,
			nullptr,
			[&]() { Loom::PixelPass::Note(passed.material); },
			[&]() { renderer.Draw(0, 0, nullptr, 0, 0, Loom::VertexFormat::Position); });

		REQUIRE(renderer.drawn.size() == 2);

		const uint32_t shown = viewport.GetTexture();
		const uint32_t drawn_into = renderer.drawnScenes[0];

		CHECK(renderer.drawn[0] == passed.shader.pixel_pass_id);
		CHECK(shown != drawn_into);
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ shown, shown });
		CHECK(renderer.targetSizes.at(shown) == VIEW);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a viewport drawn with no pixel pass shows what it drew, with what goes over it drawn into it")
	{
		RecordingRenderer renderer;
		Loom::EditorViewport viewport;

		viewport.Render(
			VIEW.x,
			VIEW.y,
			nullptr,
			[&]() { },
			[&]() { renderer.Draw(0, 0, nullptr, 0, 0, Loom::VertexFormat::Position); });

		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawnInto[0] == viewport.GetTexture());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a viewport that leaves pixel passes out shows what it drew, and notes nothing for a pass it is drawn inside")
	{
		RecordingRenderer renderer;
		Passed passed("passed");
		Loom::PixelPass outer;
		Loom::EditorViewport viewport(false);

		outer.Begin();

		viewport.Render(
			VIEW.x,
			VIEW.y,
			nullptr,
			[&]() { Loom::PixelPass::Note(passed.material); },
			[&]() { renderer.Draw(0, 0, nullptr, 0, 0, Loom::VertexFormat::Position); });

		CHECK_FALSE(outer.End());
		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawnInto[0] == viewport.GetTexture());
	};
};

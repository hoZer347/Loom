#include "doctest.h"

#include "Test Support.h"

#include "EditorViewport.h"
#include "PixelPass.h"
#include "PixelShader.h"
#include "Scene.h"
#include "Shaders.h"

#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


#pragma region Helpers

namespace
{
	const glm::ivec2 VIEW(64, 32);

	const std::string PASS =
		"out vec4 FragColor;\n"
		"void main() { FragColor = LoomScenePixel(LoomScreenPixel()); }\n";

	// A pixel shader's program, compiled from a .loompixel file of the name.
	struct Passed
	{
		explicit Passed(const std::string& name) :
			source(PASS),
			shader(name + Loom::Shader::pixel_extension, source)
		{
			Pump();
		};

		std::istringstream source;
		Loom::Shader shader;
	};
};

#pragma endregion


TEST_SUITE("PixelPass")
{
	#pragma region Passes

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a .loompixel file builds one program, the pass")
	{
		RecordingRenderer renderer;

		Passed passed("passed");

		CHECK(passed.shader.id != 0);
		CHECK(renderer.programs.size() == 1);
		CHECK(renderer.programs.at("passed.loompixel") == passed.shader.id);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a view drawn with no pixel pass is copied as it is")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(0);
		CHECK_FALSE(pass.End());

		pass.Draw(view, VIEW);

		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawnInto[0] == RecordingRenderer::WINDOW);
		CHECK(renderer.drawnScenes[0] == view);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a pass reads the view and its depth, once however many times it was noted")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed passed("passed");

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(passed.shader.id);
		Loom::PixelPass::Note(passed.shader.id);
		CHECK(pass.End());

		pass.Draw(view, VIEW);

		CHECK(renderer.drawn == std::vector<uint32_t>{ passed.shader.id });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ RecordingRenderer::WINDOW });
		CHECK(renderer.drawnScenes == std::vector<uint32_t>{ view });
		CHECK(renderer.textures.at("u_sceneDepth") == renderer.TargetDepth(view));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "passes run in the order they were noted, each reading what the one before it left")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed first("first");
		Passed second("second");
		Passed third("third");

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		Loom::PixelPass::Note(second.shader.id);
		Loom::PixelPass::Note(first.shader.id);
		Loom::PixelPass::Note(third.shader.id);
		pass.End();

		pass.Draw(view, VIEW);

		const std::vector<uint32_t> order{ second.shader.id, first.shader.id, third.shader.id };
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a pass noted while nothing is noting is not run")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed passed("passed");

		const uint32_t view = renderer.CreateColorTarget(VIEW.x, VIEW.y);

		pass.Begin();
		CHECK_FALSE(pass.End());

		Loom::PixelPass::Note(passed.shader.id);
		pass.Draw(view, VIEW);

		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawn[0] != passed.shader.id);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the window takes the pixel pass path only while a PixelShader exists")
	{
		RecordingRenderer renderer;

		CHECK_FALSE(Loom::PixelShader::Any());

		{
			Loom::Scene scene("pixel shaders");
			scene.GetRoot().AddChild("Screen")->Attach<Loom::PixelShader>();
			Pump();

			CHECK(Loom::PixelShader::Any());
		};

		// The root deletes its children a task later, and they their components a task after that.
		Pump();
		Pump();

		CHECK_FALSE(Loom::PixelShader::Any());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a PixelShader drawn notes its pass with the time, and one with no file notes nothing")
	{
		RecordingRenderer renderer;
		Loom::PixelPass pass;
		Passed passed("passed");
		Loom::Scene scene("pixel shaders");

		Loom::PixelShader* empty = scene.GetRoot().AddChild("Empty")->Attach<Loom::PixelShader>();
		Loom::PixelShader* pixel = scene.GetRoot().AddChild("Screen")->Attach<Loom::PixelShader>();
		pixel->shader = &passed.shader;
		Pump();

		pass.Begin();
		empty->OnRender();
		CHECK_FALSE(pass.End());

		pass.Begin();
		pixel->OnRender();
		CHECK(pass.End());
		CHECK(renderer.floats.contains("u_time"));
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
		Loom::PixelPass::Note(passed.shader.id);

		CHECK(outer.End());
	};

	#pragma endregion

	#pragma region Viewports

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a viewport drawn with a pixel pass shows the pass, with what goes over it drawn after it")
	{
		RecordingRenderer renderer;
		Passed passed("passed");
		Loom::EditorViewport viewport;

		viewport.Render(
			VIEW.x,
			VIEW.y,
			nullptr,
			[&]() { Loom::PixelPass::Note(passed.shader.id); },
			[&]() { renderer.Draw(0, 0, nullptr, 0, 0, Loom::VertexFormat::Position); });

		REQUIRE(renderer.drawn.size() == 2);

		const uint32_t shown = viewport.GetTexture();
		const uint32_t drawn_into = renderer.drawnScenes[0];

		CHECK(renderer.drawn[0] == passed.shader.id);
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
			[&]() { Loom::PixelPass::Note(passed.shader.id); },
			[&]() { renderer.Draw(0, 0, nullptr, 0, 0, Loom::VertexFormat::Position); });

		CHECK_FALSE(outer.End());
		REQUIRE(renderer.drawn.size() == 1);
		CHECK(renderer.drawnInto[0] == viewport.GetTexture());
	};

	#pragma endregion
};

#include "doctest.h"

#include "Test Support.h"

#include "Camera.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "SelectionOutline.h"
#include "Shaders.h"
#include "Sprite.h"

#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


namespace
{
	constexpr const char* MESH_MASK = "Loom Editor Mesh Mask: ";
	constexpr const char* SPRITE_MASK = "Loom Editor Sprite Mask";
	constexpr const char* EDGE = "Loom Editor Selection Outline";

	const glm::ivec2 VIEW(64, 32);

	const std::string STAGES =
		"// ===VERTEX===\n"
		"void main() { }\n"
		"// ===FRAGMENT===\n"
		"void main() { }\n";

	const std::vector<float> TRIANGLE{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

	// The one target the outline made, which is its mask.
	uint32_t Mask(const RecordingRenderer& renderer)
	{
		REQUIRE(renderer.targetSizes.size() == 1);
		return renderer.targetSizes.begin()->first;
	};
};


TEST_SUITE("SelectionOutline")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a selection is drawn into a mask the size of the view, children and all, then ringed once over the view")
	{
		RecordingRenderer renderer;
		renderer.windowSize = VIEW;

		std::istringstream source(STAGES);
		Loom::Shader shader("selection outline mesh", source);

		Loom::Scene scene("selection outline");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* parent = scene.AddChild("Parent");
		Pump();

		Loom::GameObject* child = parent->AddChild("Child");
		Pump();

		child->Attach<Loom::Material>()->shader = &shader;
		child->Attach<Loom::Mesh>()->m_vertices = TRIANGLE;
		Pump();

		Loom::SelectionOutline outline;
		outline.Draw(*parent, *camera);

		const uint32_t mask = Mask(renderer);

		CHECK(renderer.targetSizes.at(mask) == VIEW);
		CHECK(renderer.drawn == std::vector<uint32_t>{ renderer.programs.at(MESH_MASK + shader.file_path), renderer.programs.at(EDGE) });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ mask, RecordingRenderer::WINDOW });
		CHECK(renderer.textures["u_mask"] == mask);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh's mask is drawn with its own shader's vertex stage, so a shader that places it in clip space is ringed where it shows")
	{
		RecordingRenderer renderer;
		renderer.windowSize = VIEW;

		std::istringstream source(
			"// ===VERTEX===\n"
			"layout(location = 0) in vec3 aPos;\n"
			"void main() { gl_Position = vec4(aPos, 1.0); }\n"
			"// ===FRAGMENT===\n"
			"void main() { }\n");
		Loom::Shader shader("selection outline clip space", source);

		Loom::Scene scene("selection outline clip space");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* selected = scene.AddChild("Clip Space");
		Pump();

		selected->Attach<Loom::Material>()->shader = &shader;
		selected->Attach<Loom::Mesh>()->m_vertices = TRIANGLE;
		Pump();

		Loom::SelectionOutline outline;
		outline.Draw(*selected, *camera);
		outline.Draw(*selected, *camera);

		const std::string mask = MESH_MASK + shader.file_path;

		CHECK(renderer.vertices.at(mask) == shader.vertex);
		CHECK(renderer.drawn == std::vector<uint32_t>{
			renderer.programs.at(mask), renderer.programs.at(EDGE),
			renderer.programs.at(mask), renderer.programs.at(EDGE) });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh with no material leaves nothing to ring")
	{
		RecordingRenderer renderer;
		renderer.windowSize = VIEW;

		Loom::Scene scene("selection outline no material");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* selected = scene.AddChild("Bare");
		Pump();

		selected->Attach<Loom::Mesh>()->m_vertices = TRIANGLE;
		Pump();

		Loom::SelectionOutline outline;
		outline.Draw(*selected, *camera);

		CHECK(renderer.drawn.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sprite is cut out into the mask once it has a texture, and leaves nothing to ring before")
	{
		RecordingRenderer renderer;
		renderer.windowSize = VIEW;

		Loom::Scene scene("selection outline sprite");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* selected = scene.AddChild("Sprite");
		Pump();

		Loom::Sprite* sprite = selected->Attach<Loom::Sprite>();
		Pump();

		Loom::SelectionOutline outline;
		outline.Draw(*selected, *camera);

		CHECK(renderer.drawn.empty());

		constexpr int SIDE = 2;
		constexpr int RGBA = 4;
		constexpr uint8_t OPAQUE = 255;

		sprite->texturePath = LoomTests::WriteTga("loom selection outline sprite.tga", SIDE, SIDE, std::vector<uint8_t>(SIDE * SIDE * RGBA, OPAQUE));

		outline.Draw(*selected, *camera);

		CHECK(renderer.drawn == std::vector<uint32_t>{ renderer.programs.at(SPRITE_MASK), renderer.programs.at(EDGE) });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ Mask(renderer), RecordingRenderer::WINDOW });
	};
};

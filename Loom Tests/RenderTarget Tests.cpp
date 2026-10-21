#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "RenderTarget.h"
#include "Scene.h"
#include "Shaders.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;

// How far a projected coordinate may drift from where the math puts it.
static constexpr float EPSILON = 1e-4f;


namespace
{
	// Reads whatever its material's u_seen names.
	const std::string READER =
		"// ===VERTEX===\n"
		"uniform mat4 u_model;\n"
		"uniform mat4 u_viewProjection;\n"
		"void main() { }\n"
		"// ===FRAGMENT===\n"
		"uniform sampler2D u_seen;\n"
		"void main() { }\n";

	// A triangle with a material that draws it with shader.
	struct Drawable
	{
		Loom::GameObject* object = nullptr;
		Loom::Material* material = nullptr;
		Loom::Mesh* mesh = nullptr;

		Drawable(Loom::Scene& scene, const std::string& name, Loom::Shader& shader)
		{
			object = scene.AddChild(name);

			material = object->Attach<Loom::Material>();
			material->shader = &shader;

			mesh = object->Attach<Loom::Mesh>();
			mesh->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

			Pump();
		};
	};

	Loom::RenderTarget* TargetOn(Loom::Scene& scene, const std::string& name)
	{
		Loom::GameObject* object = scene.AddChild(name);
		Pump();

		Loom::RenderTarget* target = object->Attach<Loom::RenderTarget>();
		Pump();

		return target;
	};

	bool Near(const glm::vec4& a, const glm::vec4& b)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec4(EPSILON)));
	};
};


TEST_SUITE("RenderTarget")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a render target is a built-in component the editor can add by name")
	{
		Loom::Scene scene("render target by name");
		Pump();

		Loom::ComponentBase* target = Loom::ComponentRegistry::Create("RenderTarget", scene.GetRoot());
		Pump();

		REQUIRE(target != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::RenderTarget>() == target);
		CHECK(Loom::ComponentRegistry::CategoryOf("RenderTarget") == "Rendering");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a target is made the first time it is used, as its fields say, and cleared to its colour")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("made on first use");
		Loom::RenderTarget* target = TargetOn(scene, "Target");

		target->width = 64;
		target->height = 32;
		target->pixelated = true;
		target->clearColor = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);

		CHECK(renderer.targetSizes.empty());

		const uint32_t texture = target->Texture();

		REQUIRE(texture != 0);
		CHECK(renderer.targetSizes[texture] == glm::ivec2(64, 32));
		CHECK_FALSE(renderer.targetDepths[texture]);
		CHECK(renderer.targetsPixelated[texture]);

		REQUIRE(renderer.clears.size() == 1);
		CHECK(renderer.clearedInto[0] == texture);
		CHECK(renderer.clearColors[0] == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
		CHECK_FALSE(renderer.clears[0].second);

		// Used again as it is, it is the same texture, not cleared again.
		CHECK(target->Texture() == texture);
		CHECK(renderer.clears.size() == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "changing its size, depth or pixelated makes it again and lets the old one go")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("made again");
		Loom::RenderTarget* target = TargetOn(scene, "Target");

		uint32_t texture = target->Texture();

		const auto remade = [&]()
			{
				const uint32_t previous = texture;
				texture = target->Texture();

				return texture != previous && renderer.destroyedTextures.back() == previous;
			};

		target->width = 128;
		CHECK(remade());
		CHECK(renderer.targetSizes[texture].x == 128);

		target->depth = true;
		CHECK(remade());
		CHECK(renderer.targetDepths[texture]);

		target->pixelated = true;
		CHECK(remade());

		// Its clear colour is only what Clear clears to, and makes nothing.
		target->clearColor = glm::vec4(1.0f);
		CHECK(target->Texture() == texture);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "its size is kept to what the renderer can make")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("kept to size");
		Loom::RenderTarget* target = TargetOn(scene, "Target");

		target->width = 0;
		target->height = RecordingRenderer::MAX_TEXTURE_SIZE * 2;

		CHECK(renderer.targetSizes[target->Texture()] == glm::ivec2(1, RecordingRenderer::MAX_TEXTURE_SIZE));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a target clears only when told, to its colour, and its depth only when it has one")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("cleared when told", source);

		Loom::Scene scene("cleared when told");
		Loom::RenderTarget* target = TargetOn(scene, "Target");
		Drawable drawn(scene, "Drawn", shader);

		const uint32_t texture = target->Texture();
		const auto clears = [&]() { return std::count(renderer.clearedInto.begin(), renderer.clearedInto.end(), texture); };

		target->Draw(*drawn.mesh, glm::mat4(1.0f));
		target->Draw(*drawn.mesh, glm::mat4(1.0f));

		// The one it was made with.
		CHECK(clears() == 1);

		target->clearColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f);
		target->Clear();

		REQUIRE(clears() == 2);
		CHECK(renderer.clearedInto.back() == texture);
		CHECK(renderer.clearColors.back() == glm::vec4(0.0f, 1.0f, 0.0f, 0.5f));
		CHECK_FALSE(renderer.clears.back().second);

		target->depth = true;
		target->Clear();

		CHECK(renderer.clears.back().second);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh drawn into a target is placed and seen as it is told, blended as it is told")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("drawn into a target", source);

		Loom::Scene scene("drawn into a target");
		Loom::RenderTarget* target = TargetOn(scene, "Target");
		Drawable drawn(scene, "Drawn", shader);

		const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
		const glm::mat4 viewProjection = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));

		target->Draw(*drawn.mesh, *drawn.material, { model }, viewProjection, Loom::Blend::Max);

		CHECK(renderer.drawn == std::vector<uint32_t>{ shader.id });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ target->Texture() });
		CHECK(renderer.matrices["u_model"] == model);
		CHECK(renderer.matrices["u_viewProjection"] == viewProjection);

		// The blend goes back to alpha after, for whatever is drawn next.
		CHECK(renderer.blends == std::vector<Loom::Blend>{ Loom::Blend::Max, Loom::Blend::Alpha });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh drawn at many places is drawn once at each, all into the target")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("drawn at many places", source);

		Loom::Scene scene("drawn at many places");
		Loom::RenderTarget* target = TargetOn(scene, "Target");
		Drawable drawn(scene, "Drawn", shader);

		const std::vector<glm::mat4> models =
		{
			glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
			glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)),
		};

		target->Draw(*drawn.mesh, models, glm::mat4(1.0f), Loom::Blend::Add);

		CHECK(renderer.drawnInto == std::vector<uint32_t>(models.size(), target->Texture()));
		CHECK(renderer.matrices["u_model"] == models.back());
		CHECK(renderer.blends == std::vector<Loom::Blend>{ Loom::Blend::Add, Loom::Blend::Alpha });

		// None at all draws nothing.
		target->Draw(*drawn.mesh, *drawn.material, std::vector<glm::mat4>{ }, glm::mat4(1.0f));
		CHECK(renderer.drawn.size() == models.size());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh drawn on its own goes where its GameObject is, with its own material")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("drawn where it is", source);

		Loom::Scene scene("drawn where it is");
		Loom::RenderTarget* target = TargetOn(scene, "Target");
		Drawable drawn(scene, "Drawn", shader);

		drawn.object->transform.position = glm::vec3(4.0f, 5.0f, 6.0f);

		target->Draw(*drawn.mesh, glm::mat4(1.0f));

		CHECK(renderer.drawn == std::vector<uint32_t>{ shader.id });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ target->Texture() });
		CHECK(renderer.matrices["u_model"] == drawn.object->WorldMatrix());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with no shader draws nothing into a target, and makes nothing")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("no shader");
		Loom::RenderTarget* target = TargetOn(scene, "Target");

		Loom::GameObject* object = scene.AddChild("Unshaded");
		Loom::Material* material = object->Attach<Loom::Material>();
		Loom::Mesh* mesh = object->Attach<Loom::Mesh>();
		Pump();

		target->Draw(*mesh, *material, { glm::mat4(1.0f) }, glm::mat4(1.0f));
		target->Draw(*mesh, { glm::mat4(1.0f) }, glm::mat4(1.0f));
		target->Draw(*mesh, glm::mat4(1.0f));

		CHECK(renderer.drawn.empty());
		CHECK(renderer.targetSizes.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material reads the target its sampler names by its GameObject's name")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("reads a target", source);

		Loom::Scene scene("reads a target");
		Drawable reader(scene, "Reader", shader);
		reader.material->variables->SetTexture("u_seen", "Seen");

		// Before there is one, the sampler still has something behind it.
		reader.material->Apply(shader.id);
		const uint32_t blank = renderer.textures["u_seen"];
		CHECK(blank != 0);

		Loom::GameObject* seen = scene.AddChild("Seen");
		Pump();

		Loom::RenderTarget* target = seen->Attach<Loom::RenderTarget>();
		Pump();

		reader.material->Apply(shader.id);
		CHECK(renderer.textures["u_seen"] == target->Texture());
		CHECK(Loom::RenderTarget::Named("Seen") == target);

		// Gone, it is let go and named no more.
		const uint32_t texture = target->Texture();
		seen->DetachComponent(target);
		Pump();

		CHECK(Loom::RenderTarget::Named("Seen") == nullptr);
		CHECK(renderer.destroyedTextures.back() == texture);

		reader.material->Apply(shader.id);
		CHECK(renderer.textures["u_seen"] == blank);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a target being drawn into reads as white to what is drawn into it")
	{
		RecordingRenderer renderer;

		std::istringstream source(READER);
		Loom::Shader shader("reads itself", source);

		Loom::Scene scene("reads itself");
		Loom::RenderTarget* target = TargetOn(scene, "Seen");
		Drawable reader(scene, "Reader", shader);

		// What a sampler naming nothing reads.
		reader.material->Apply(shader.id);
		const uint32_t blank = renderer.textures["u_seen"];

		reader.material->variables->SetTexture("u_seen", "Seen");
		target->Draw(*reader.mesh, glm::mat4(1.0f));

		CHECK(renderer.textures["u_seen"] == blank);
		CHECK(blank != target->Texture());
		CHECK_FALSE(target->IsDrawnInto());

		reader.material->Apply(shader.id);
		CHECK(renderer.textures["u_seen"] == target->Texture());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "with no renderer a target makes and draws nothing")
	{
		// Only to compile the shader with.
		std::optional<RecordingRenderer> compiler(std::in_place);

		std::istringstream source(READER);
		Loom::Shader shader("no renderer", source);

		compiler.reset();

		Loom::Scene scene("no renderer");
		Loom::RenderTarget* target = TargetOn(scene, "Target");
		Drawable drawn(scene, "Drawn", shader);

		CHECK(target->Texture() == 0);

		target->Clear();
		target->Draw(*drawn.mesh, glm::mat4(1.0f));

		CHECK(target->Texture() == 0);
	};

	TEST_CASE("the overhead view lays world XZ over the target, the higher in front")
	{
		const glm::vec2 min(-10.0f, 20.0f);
		const glm::vec2 max(30.0f, 60.0f);
		const glm::mat4 overhead = Loom::RenderTarget::Overhead(min, max);

		const auto clip = [&overhead](float x, float y, float z) { return overhead * glm::vec4(x, y, z, 1.0f); };

		CHECK(Near(clip(min.x, 0.0f, min.y), glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f)));
		CHECK(Near(clip(max.x, 0.0f, max.y), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)));
		CHECK(Near(clip(10.0f, 0.0f, 40.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));

		// Its whole height is in view, the top nearest.
		CHECK(std::abs(clip(10.0f, Loom::RenderTarget::overheadHeight, 40.0f).z + 1.0f) < EPSILON);
		CHECK(std::abs(clip(10.0f, -Loom::RenderTarget::overheadHeight, 40.0f).z - 1.0f) < EPSILON);
		CHECK(clip(10.0f, 1.0f, 40.0f).z < clip(10.0f, 0.0f, 40.0f).z);
	};
};

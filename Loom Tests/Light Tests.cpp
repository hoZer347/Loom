#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Light.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include "glm/glm.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;
using LoomTests::EPSILON;


namespace
{
	// A component that notes which light was current when it was drawn.
	struct LightWitness final : Loom::Component<LightWitness>
	{
		void OnRender() override { seen = Loom::Light::current; };

		Loom::Light* seen = nullptr;
	};

	// A scene with a Light as files wrote it when it had a direction at place
	// 0, straight down, and an ambient of 0.5.
	std::string OlderFile()
	{
		constexpr std::string_view DIRECTION_LINE = "\t\t\t\t0 = (0, -1, 0)\n";

		Loom::Scene scene("older file");
		Pump();

		Loom::Light* written = scene.AddChild("Sun")->Attach<Loom::Light>();
		written->ambient = 0.5f;
		Pump();

		std::string text = Loom::SceneSerializer::Serialize(scene);
		const size_t light_line = text.find("Component Light");
		REQUIRE(light_line != std::string::npos);

		text.insert(text.find('\n', light_line) + 1, DIRECTION_LINE);

		return text;
	};
};


TEST_SUITE("Light")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene with no light of its own is lit by another open scene's")
	{
		RecordingRenderer renderer;

		Loom::Scene lit("lit");
		Loom::Scene unlit("unlit");
		Pump();

		Loom::Light* light = lit.Attach<Loom::Light>();
		LightWitness* witness = unlit.Attach<LightWitness>();
		Pump();

		unlit.Render();
		CHECK(witness->seen == light);

		// One whose own light is off stays unlit rather than borrowing.
		Loom::GameObject* holder = unlit.AddChild("Light");
		Pump();

		holder->Attach<Loom::Light>();
		holder->SetDisabled(true);
		Pump();

		unlit.Render();
		CHECK(witness->seen == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a light is a built-in component the editor can add by name")
	{
		Loom::Scene scene("light by name");
		Pump();

		Loom::ComponentBase* light = Loom::ComponentRegistry::Create("Light", scene.GetRoot());
		Pump();

		REQUIRE(light != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::Light>() == light);
		CHECK(Loom::ComponentRegistry::NameOf(*light) == "Light");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene without a light has none to find")
	{
		Loom::Scene scene("unlit");
		scene.AddChild("Empty");
		Pump();

		CHECK(scene.GetRoot().FindComponent<Loom::Light>() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "FindComponent reaches a light anywhere under the root")
	{
		Loom::Scene scene("nested light");
		Loom::GameObject* parent = scene.AddChild("Parent");
		Pump();

		Loom::GameObject* child = parent->AddChild("Child");
		Pump();

		Loom::Light* light = child->Attach<Loom::Light>();
		Pump();

		CHECK(scene.GetRoot().FindComponent<Loom::Light>() == light);

		// What the scene lights with, so a disabled ancestor turns it off.
		parent->SetDisabled(true);

		CHECK(scene.GetRoot().FindComponent<Loom::Light>() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the root's own light comes before a child's")
	{
		Loom::Scene scene("two lights");
		Loom::GameObject* child = scene.AddChild("Child");
		Pump();

		child->Attach<Loom::Light>();
		Loom::Light* root_light = scene.Attach<Loom::Light>();
		Pump();

		CHECK(scene.GetRoot().FindComponent<Loom::Light>() == root_light);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the shadow map is centred on shadowCenter and looks along the light's rotation")
	{
		Loom::Scene scene("shadow map");
		Pump();

		Loom::GameObject* sun = scene.AddChild("Sun");
		Loom::Light& light = *sun->Attach<Loom::Light>();
		Pump();

		sun->transform.rotation = Loom::Light::RotationAlong(glm::vec3(0.0f, -1.0f, 0.0f));
		light.shadowCenter = glm::vec3(1.0f, 2.0f, 3.0f);
		light.shadowExtent = 4.0f;

		const glm::mat4 transform = light.ViewProjection();

		const glm::vec4 center = transform * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
		CHECK(std::abs(center.x) < EPSILON);
		CHECK(std::abs(center.y) < EPSILON);
		CHECK(std::abs(center.z) < EPSILON);

		// Above the centre is toward a light shining down, so nearer.
		const glm::vec4 above = transform * glm::vec4(1.0f, 3.0f, 3.0f, 1.0f);
		CHECK(above.z < center.z);

		// One extent out sideways is the edge of the map.
		const glm::vec4 edge = transform * glm::vec4(5.0f, 2.0f, 3.0f, 1.0f);
		CHECK(std::abs(std::abs(edge.x) - 1.0f) < EPSILON);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the shadow pass pulls casters toward the light by their slope up to a limit, pushed back by a constant, and clears it after")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("depth bias");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();
		Pump();

		light->RenderShadowMap({ &scene.GetRoot() });

		REQUIRE(renderer.depthBiases.size() == 2);

		const auto [slope, constant, clamp] = renderer.depthBiases.front();
		CHECK(slope < 0.0f);
		CHECK(constant > 0.0f);
		CHECK(clamp < 0.0f);
		CHECK(renderer.depthBiases.back() == std::tuple<float, float, float>{ 0.0f, 0.0f, 0.0f });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a light shines along its GameObject's -Z, turned by its parents")
	{
		Loom::Scene scene("direction");
		Pump();

		Loom::GameObject* parent = scene.AddChild("Parent");
		Loom::GameObject* sun = parent->AddChild("Sun");
		Loom::Light* light = sun->Attach<Loom::Light>();
		Pump();

		CHECK(glm::length(light->Direction() - glm::vec3(0.0f, 0.0f, -1.0f)) < EPSILON);

		constexpr float QUARTER_TURN = 90.0f;
		parent->transform.rotation = glm::vec3(0.0f, QUARTER_TURN, 0.0f);

		CHECK(glm::length(light->Direction() - glm::vec3(-1.0f, 0.0f, 0.0f)) < EPSILON);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "RotationAlong aims a light along any direction")
	{
		Loom::Scene scene("rotation along");
		Pump();

		Loom::GameObject* sun = scene.AddChild("Sun");
		Loom::Light* light = sun->Attach<Loom::Light>();
		Pump();

		const glm::vec3 DIRECTIONS[] =
		{
			Loom::Light::DefaultDirection,
			{ 0.0f, -1.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
			{ -1.0f, -0.5f, -0.3f },
		};

		for (const glm::vec3& direction : DIRECTIONS)
		{
			sun->transform.rotation = Loom::Light::RotationAlong(direction);

			CHECK(glm::length(light->Direction() - glm::normalize(direction)) < EPSILON);
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a direction saved by an older file becomes the rotation, and the rest keep their places")
	{
		const std::unique_ptr<Loom::Scene> loaded(Loom::SceneSerializer::Deserialize(OlderFile()));
		Pump();

		REQUIRE(loaded);
		const Loom::Light* light = loaded->GetRoot().FindComponent<Loom::Light>();
		REQUIRE(light != nullptr);

		CHECK(glm::length(light->Direction() - glm::vec3(0.0f, -1.0f, 0.0f)) < EPSILON);
		CHECK(light->ambient == 0.5f);

		const std::string saved = Loom::SceneSerializer::Serialize(*loaded);
		const size_t saved_line = saved.find("Component Light");
		CHECK(saved.find("\t0 = (0, -1, 0)", saved_line) == std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a direction read a line at a time with frames between still becomes the rotation")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "Loom older light.loomscene";
		std::ofstream(path, std::ios::binary) << OlderFile();

		const std::unique_ptr<Loom::SceneSerializer::Loading> loading = Loom::SceneSerializer::BeginLoadFromFile(path.string());
		REQUIRE(loading);

		while (loading->Step(0.0))
			Pump();

		const std::unique_ptr<Loom::Scene> loaded(loading->Finish());
		Pump();
		std::filesystem::remove(path);

		REQUIRE(loaded);
		const Loom::Light* light = loaded->GetRoot().FindComponent<Loom::Light>();
		REQUIRE(light != nullptr);

		CHECK(glm::length(light->Direction() - glm::vec3(0.0f, -1.0f, 0.0f)) < EPSILON);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a transform scaled to nothing still gives a usable shadow map")
	{
		Loom::Scene scene("zero scale");
		Pump();

		Loom::GameObject* sun = scene.AddChild("Sun");
		Loom::Light* light = sun->Attach<Loom::Light>();
		Pump();

		sun->transform.scale = glm::vec3();

		const glm::mat4 transform = light->ViewProjection();

		for (int column = 0; column < 4; column++)
			for (int row = 0; row < 4; row++)
				CHECK(std::isfinite(transform[column][row]));
	};

	TEST_CASE("a stage that never lights itself is compiled as written")
	{
		const std::string source = "void main() { gl_Position = vec4(0.0); }";

		CHECK(Loom::Light::VertexLibrary(source).empty());
		CHECK(Loom::Light::FragmentLibrary(source).empty());
	};

	TEST_CASE("calling into the light's GLSL brings it in front of that stage")
	{
		const std::string vertex = Loom::Light::VertexLibrary("v = LoomLightSpace(world);");
		CHECK(vertex.find("vec4 LoomLightSpace(") != std::string::npos);
		CHECK(vertex.find("u_lightViewProjection") != std::string::npos);

		// LoomLight calls LoomShadow, so either one brings in both.
		for (const char* call : { "c = LoomLight(n, l);", "s = LoomShadow(l);" })
		{
			const std::string fragment = Loom::Light::FragmentLibrary(call);

			CHECK(fragment.find("float LoomShadow(") != std::string::npos);
			CHECK(fragment.find("vec3 LoomLight(") != std::string::npos);
			CHECK(fragment.find("u_shadowMap") != std::string::npos);
		};

		// A vertex stage never shades, while a fragment stage may read its own light space.
		CHECK(Loom::Light::VertexLibrary("c = LoomLight(n, l);").empty());
		CHECK(Loom::Light::FragmentLibrary("v = LoomLightSpace(world);").find("vec4 LoomLightSpace(") != std::string::npos);
	};

	// What counts is a call to that exact name, however it is spaced.
	TEST_CASE("only a whole function name followed by a call brings the library in")
	{
		CHECK_FALSE(Loom::Light::FragmentLibrary("c = LoomLight (n, l);").empty());
		CHECK_FALSE(Loom::Light::FragmentLibrary("c = LoomLight\n\t(n, l);").empty());

		CHECK(Loom::Light::FragmentLibrary("c = MyLoomLight(n, l);").empty());
		CHECK(Loom::Light::FragmentLibrary("// LoomLight is not called here").empty());
	};
};

#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Light.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <cmath>

using LoomTests::Pump;

// How far a projected coordinate may drift from where the math puts it.
static constexpr float EPSILON = 1e-4f;


TEST_SUITE("Light")
{
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the shadow map is centred on shadowCenter and looks along direction")
	{
		Loom::Light light;
		Pump();

		light.direction = Loom::Math::vec3<float>(0.0f, -1.0f, 0.0f);
		light.shadowCenter = Loom::Math::vec3<float>(1.0f, 2.0f, 3.0f);
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a zero direction still gives a usable transform")
	{
		Loom::Light light;
		Pump();

		light.direction = Loom::Math::vec3<float>();

		const glm::mat4 transform = light.ViewProjection();

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

		// Each stage only answers to its own functions.
		CHECK(Loom::Light::FragmentLibrary("v = LoomLightSpace(world);").empty());
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

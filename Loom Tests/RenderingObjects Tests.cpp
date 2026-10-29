#include "doctest.h"

#include "Test Support.h"

#include "RenderingObjects.h"

#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "ProjectTemplate.h"
#include "Scene.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>


namespace
{
	namespace RenderingObjects = Loom::RenderingObjects;

	constexpr const char* SHADER = "Assets/Unlit.shader";

	// How far off a float may come out of the trigonometry and still count.
	constexpr float CLOSE = 1e-4f;

	Loom::GameObject* Named(Loom::GameObject& parent, const std::string& name)
	{
		for (Loom::GameObject* child : parent.GetChildren())
			if (child->GetName() == name)
				return child;

		return nullptr;
	};
};

TEST_SUITE("RenderingObjects")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the Rendering menu's Light and Camera are children with a Light and a Camera")
	{
		Loom::Scene scene("light and camera menu");
		LoomTests::Pump();

		Loom::GameObject* light = RenderingObjects::AddLight(scene.GetRoot());
		Loom::GameObject* camera = RenderingObjects::AddCamera(scene.GetRoot());
		LoomTests::Pump();

		REQUIRE(light != nullptr);
		REQUIRE(camera != nullptr);
		CHECK(light->GetName() == RenderingObjects::LightLabel);
		CHECK(camera->GetName() == RenderingObjects::CameraLabel);
		CHECK(light->GetParent() == &scene.GetRoot());
		CHECK(camera->GetParent() == &scene.GetRoot());
		CHECK(light->GetComponent<Loom::Light>() != nullptr);
		CHECK(camera->GetComponent<Loom::Camera>() != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a starter scene has a light, and a camera looking down at 45 degrees at the middle of a plane")
	{
		Loom::Scene scene("starter");
		LoomTests::Pump();

		RenderingObjects::AddStarter(scene.GetRoot(), SHADER);
		LoomTests::Pump();

		CHECK(scene.GetRoot().GetChildren().size() == 3);

		Loom::GameObject* light = Named(scene.GetRoot(), RenderingObjects::LightLabel);
		Loom::GameObject* camera = Named(scene.GetRoot(), RenderingObjects::CameraLabel);
		Loom::GameObject* plane = Named(scene.GetRoot(), "Plane");

		REQUIRE(light != nullptr);
		REQUIRE(camera != nullptr);
		REQUIRE(plane != nullptr);
		CHECK(light->GetComponent<Loom::Light>() != nullptr);
		REQUIRE(camera->GetComponent<Loom::Camera>() != nullptr);
		CHECK(plane->GetComponent<Loom::Mesh>() != nullptr);

		const glm::vec3 eye = *camera->transform.position;
		const glm::vec3 forward = Loom::Transform::Rotation(*camera->transform.rotation) * glm::vec3(0.0f, 0.0f, -1.0f);

		CHECK(forward.y == doctest::Approx(forward.z).epsilon(CLOSE));
		CHECK(forward.y == doctest::Approx(-std::sqrt(0.5f)).epsilon(CLOSE));
		CHECK(forward.x == doctest::Approx(0.0f).epsilon(CLOSE));

		const glm::vec3 seen = eye + forward * (eye.y / -forward.y);

		CHECK(seen.x == doctest::Approx(plane->transform.position->x).epsilon(CLOSE));
		CHECK(seen.z == doctest::Approx(plane->transform.position->z).epsilon(CLOSE));
		CHECK(plane->transform.scale->x > 1.0f);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a starter scene with no shader, as when no project is open, has a plane with no shader and says nothing")
	{
		Loom::Scene scene("starter without a project");
		LoomTests::Pump();

		std::ostringstream errors;
		std::streambuf* const previous = std::cerr.rdbuf(errors.rdbuf());

		RenderingObjects::AddStarter(scene.GetRoot(), "");
		LoomTests::Pump();

		std::cerr.rdbuf(previous);

		Loom::GameObject* plane = Named(scene.GetRoot(), "Plane");

		REQUIRE(plane != nullptr);
		REQUIRE(plane->GetComponent<Loom::Material>() != nullptr);
		CHECK(plane->GetComponent<Loom::Material>()->GetShaderPath().empty());
		CHECK(errors.str().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a new project's Main scene is the starter scene, its shader found in the project")
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "loom starter project";
		std::filesystem::remove_all(folder);

		std::ostringstream errors;
		std::streambuf* const previous = std::cerr.rdbuf(errors.rdbuf());

		const std::string project = Loom::ProjectTemplate::Create(folder.string(), "Starter");
		LoomTests::Pump();

		std::cerr.rdbuf(previous);

		CHECK_MESSAGE(errors.str().find("could not open") == std::string::npos, errors.str());

		REQUIRE_FALSE(project.empty());

		std::ifstream file(folder / "Starter" / "Scenes" / "Main.loomscene");
		const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		file.close();

		CHECK(text.rfind("Main(", 0) == 0);
		CHECK(text.find("GameObject Light(") != std::string::npos);
		CHECK(text.find("Component Light(") != std::string::npos);
		CHECK(text.find("GameObject Camera(") != std::string::npos);
		CHECK(text.find("Component Camera(") != std::string::npos);
		CHECK(text.find("GameObject Plane(") != std::string::npos);
		CHECK(text.find(Loom::ProjectTemplate::shader_path) != std::string::npos);

		std::filesystem::remove_all(folder);
	};
};

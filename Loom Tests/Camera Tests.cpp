#include "doctest.h"

#include "Test Support.h"

#include "Camera.h"
#include "ComponentRegistry.h"
#include "RenderMath.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <cmath>

using LoomTests::Pump;

// How far a projected coordinate may drift from where the math puts it.
static constexpr float EPSILON = 1e-4f;

static constexpr float SQUARE = 1.0f;
static constexpr float WIDE = 2.0f;

static void CheckSame(const glm::mat4& actual, const glm::mat4& expected)
{
	for (int column = 0; column < 4; column++)
		for (int row = 0; row < 4; row++)
			CHECK(actual[column][row] == doctest::Approx(expected[column][row]).epsilon(EPSILON));
};


TEST_SUITE("Camera")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a camera is a built-in component the editor can add by name")
	{
		Loom::Scene scene("camera by name");
		Pump();

		Loom::ComponentBase* camera = Loom::ComponentRegistry::Create("Camera", scene.GetRoot());
		Pump();

		REQUIRE(camera != nullptr);
		CHECK(scene.GetRoot().FindComponent<Loom::Camera>() == camera);
		CHECK(Loom::ComponentRegistry::NameOf(*camera) == "Camera");
	};

	// Camera::current only means anything while Scene::Render is drawing.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rendering leaves no camera current")
	{
		Loom::Scene scene("current camera");
		Pump();

		scene.Attach<Loom::Camera>();
		Pump();

		scene.Render();

		CHECK(Loom::Camera::current == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a camera looks down its transform's -Z, nearer things in front")
	{
		Loom::Scene scene("transform camera");
		Loom::GameObject* object = scene.AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		object->transform.position = glm::vec3(0.0f, 0.0f, 5.0f);

		const glm::mat4 transform = camera->ViewProjection(SQUARE);

		const glm::vec4 ahead = transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		CHECK(std::abs(ahead.x / ahead.w) < EPSILON);
		CHECK(std::abs(ahead.y / ahead.w) < EPSILON);

		const glm::vec4 nearer = transform * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		CHECK(nearer.z / nearer.w < ahead.z / ahead.w);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "looking at a target puts it in the middle of the view, whatever the rotation")
	{
		Loom::Scene scene("target camera");
		Loom::GameObject* object = scene.AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		object->transform.position = glm::vec3(3.0f, 1.0f, 4.0f);
		object->transform.rotation = glm::vec3(0.0f, 180.0f, 0.0f);
		camera->lookAtTarget = true;
		camera->target = glm::vec3(-1.0f, 2.0f, 0.0f);

		const glm::vec4 target = camera->ViewProjection(SQUARE) * glm::vec4(-1.0f, 2.0f, 0.0f, 1.0f);
		CHECK(std::abs(target.x / target.w) < EPSILON);
		CHECK(std::abs(target.y / target.w) < EPSILON);
		CHECK(target.w > 0.0f);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a wider target squeezes x and leaves y alone")
	{
		Loom::Camera camera;
		Pump();

		const glm::vec4 point = glm::vec4(1.0f, 1.0f, -5.0f, 1.0f);

		const glm::vec4 square = camera.ViewProjection(SQUARE) * point;
		const glm::vec4 wide = camera.ViewProjection(WIDE) * point;

		CHECK(std::abs(wide.x / wide.w * WIDE - square.x / square.w) < EPSILON);
		CHECK(std::abs(wide.y / wide.w - square.y / square.w) < EPSILON);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a camera looking straight down still gives a usable transform")
	{
		Loom::Scene scene("straight down");
		Loom::GameObject* object = scene.AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		object->transform.position = glm::vec3(0.0f, 10.0f, 0.0f);
		camera->lookAtTarget = true;
		camera->target = glm::vec3(0.0f, 0.0f, 0.0f);

		const glm::mat4 transform = camera->ViewProjection(SQUARE);

		for (int column = 0; column < 4; column++)
			for (int row = 0; row < 4; row++)
				CHECK(std::isfinite(transform[column][row]));

		const glm::vec4 target = transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		CHECK(std::abs(target.x / target.w) < EPSILON);
		CHECK(std::abs(target.y / target.w) < EPSILON);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "turning the target on leaves the view where it was, the target as far away as it was")
	{
		Loom::Scene scene("toggle on");
		Loom::GameObject* parent = scene.AddChild("Rig");
		Pump();

		Loom::GameObject* object = parent->AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		parent->transform.rotation = glm::vec3(10.0f, -40.0f, 5.0f);
		parent->transform.scale = glm::vec3(2.0f, 1.0f, 3.0f);
		object->transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
		object->transform.rotation = glm::vec3(20.0f, 30.0f, 15.0f);
		camera->target = glm::vec3(4.0f, -2.0f, 1.0f);

		const glm::vec3 eye = glm::vec3(object->WorldMatrix()[3]);
		const float distance = glm::length(glm::vec3(4.0f, -2.0f, 1.0f) - eye);
		const glm::mat4 before = camera->ViewProjection(SQUARE);

		camera->SetLookAtTarget(true);

		CHECK(camera->lookAtTarget);
		CheckSame(camera->ViewProjection(SQUARE), before);
		CHECK(glm::length(*camera->target - eye) == doctest::Approx(distance).epsilon(EPSILON));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "turning the target off turns the transform to face it, under a turned parent")
	{
		Loom::Scene scene("toggle off");
		Loom::GameObject* parent = scene.AddChild("Rig");
		Pump();

		Loom::GameObject* object = parent->AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		parent->transform.position = glm::vec3(-2.0f, 0.0f, 1.0f);
		parent->transform.rotation = glm::vec3(-25.0f, 70.0f, 0.0f);
		parent->transform.scale = glm::vec3(1.0f, 1.0f, 2.0f);
		object->transform.position = glm::vec3(0.0f, 3.0f, 6.0f);
		object->transform.rotation = glm::vec3(0.0f, 0.0f, 30.0f);
		object->transform.scale = glm::vec3(1.0f, 4.0f, 0.5f);
		camera->lookAtTarget = true;
		camera->target = glm::vec3(5.0f, -1.0f, 2.0f);

		const glm::mat4 before = camera->ViewProjection(SQUARE);

		camera->SetLookAtTarget(false);

		CHECK_FALSE(camera->lookAtTarget);
		CheckSame(camera->ViewProjection(SQUARE), before);
		CHECK(*camera->target == glm::vec3(5.0f, -1.0f, 2.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "turning the target on and off again leaves the rotation as it was")
	{
		Loom::Scene scene("round trip");
		Loom::GameObject* object = scene.AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		const glm::vec3 rotation(20.0f, 30.0f, 15.0f);
		object->transform.rotation = rotation;

		camera->SetLookAtTarget(true);
		camera->SetLookAtTarget(false);

		const glm::vec3 after = *object->transform.rotation;

		for (int axis = 0; axis < 3; axis++)
			CHECK(after[axis] == doctest::Approx(rotation[axis]).epsilon(EPSILON));
	};

	// The inspector writes the checkbox straight into the field, then says so.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the inspector flipping the field re-aims the camera the same way")
	{
		Loom::Scene scene("inspector toggle");
		Loom::GameObject* object = scene.AddChild("Camera");
		Pump();

		Loom::Camera* camera = object->Attach<Loom::Camera>();
		Pump();

		object->transform.rotation = glm::vec3(-30.0f, 45.0f, 0.0f);

		const glm::mat4 before = camera->ViewProjection(SQUARE);

		for (const Loom::SerializedField& field : camera->GetFields())
			if (field.data == &*camera->lookAtTarget)
			{
				*(bool*)field.data = true;
				camera->OnFieldChanged(field);
			};

		CHECK(camera->lookAtTarget);
		CheckSame(camera->ViewProjection(SQUARE), before);
	};
};

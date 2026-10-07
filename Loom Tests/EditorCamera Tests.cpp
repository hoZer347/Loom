#include "doctest.h"

#include "Test Support.h"

#include "EditorCamera.h"

#include "imgui.h"

#include "glm/glm.hpp"

#include <cmath>

using LoomTests::Pump;

namespace
{
	constexpr float EPSILON = 1e-4f;

	constexpr int fly_frames = 30;
	constexpr float pan_pixels = 10.0f;

	// Far more than it takes to turn straight up.
	constexpr float look_pixels = 100000.0f;

	constexpr float max_pitch_degrees = 89.0f;

	// An ImGui context of the test's own, with no window and no renderer, and
	// a camera to drive through it.
	struct Driven : LoomTests::EngineFixture
	{
		Driven()
		{
			constexpr float width = 800.0f;
			constexpr float height = 600.0f;
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			// Every event lands in the frame it was sent before.
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			io.AddMousePosEvent(width / 2.0f, height / 2.0f);

			// The camera registers itself on the task queue.
			Pump();
		};

		~Driven()
		{
			ImGui::DestroyContext(context);
		};

		void Frame(bool hovered, bool held)
		{
			ImGui::NewFrame();
			camera.Drive(hovered, held);
			ImGui::EndFrame();
		};

		void MoveMouse(float dx, float dy)
		{
			const ImVec2 at = ImGui::GetIO().MousePos;

			ImGui::GetIO().AddMousePosEvent(at.x + dx, at.y + dy);
		};

		glm::vec3 Eye() { return glm::vec3(camera.Get()->GetPose()[3]); };
		glm::vec3 Forward() { return -glm::vec3(camera.Get()->GetPose()[2]); };

		Loom::EditorCamera camera;
		ImGuiContext* context = nullptr;
	};

	void CheckSame(const glm::vec3& actual, const glm::vec3& expected)
	{
		for (int axis = 0; axis < 3; axis++)
			CHECK(actual[axis] == doctest::Approx(expected[axis]).epsilon(EPSILON));
	};
};


TEST_SUITE("EditorCamera")
{
	TEST_CASE_FIXTURE(Driven, "holding the right button, W flies the way the camera faces")
	{
		Frame(true, false);

		const glm::vec3 start = Eye();
		const glm::vec3 forward = Forward();

		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_W, true);

		for (int frame = 0; frame < fly_frames; frame++)
			Frame(true, true);

		const glm::vec3 moved = Eye() - start;

		REQUIRE(glm::length(moved) > 0.0f);
		CheckSame(glm::normalize(moved), forward);
	};

	TEST_CASE_FIXTURE(Driven, "W does nothing without the right button")
	{
		Frame(true, false);

		const glm::vec3 start = Eye();

		ImGui::GetIO().AddKeyEvent(ImGuiKey_W, true);

		for (int frame = 0; frame < fly_frames; frame++)
			Frame(true, true);

		CheckSame(Eye(), start);
	};

	TEST_CASE_FIXTURE(Driven, "the wheel moves in only over the view")
	{
		Frame(true, false);

		const glm::vec3 start = Eye();
		const glm::vec3 forward = Forward();

		ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
		Frame(false, false);

		CheckSame(Eye(), start);

		ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
		Frame(true, false);

		const glm::vec3 moved = Eye() - start;

		REQUIRE(glm::length(moved) > 0.0f);
		CheckSame(glm::normalize(moved), forward);
	};

	TEST_CASE_FIXTURE(Driven, "the middle button pans against the mouse without turning")
	{
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
		Frame(true, true);

		const glm::vec3 start = Eye();
		const glm::vec3 forward = Forward();
		const glm::vec3 right(camera.Get()->GetPose()[0]);

		MoveMouse(pan_pixels, 0.0f);
		Frame(true, true);

		const glm::vec3 moved = Eye() - start;

		REQUIRE(glm::length(moved) > 0.0f);
		CheckSame(glm::normalize(moved), -right);
		CheckSame(Forward(), forward);
	};

	TEST_CASE_FIXTURE(Driven, "looking up stops short of straight up")
	{
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
		Frame(true, true);

		MoveMouse(0.0f, -look_pixels);
		Frame(true, true);

		CHECK(Forward().y == doctest::Approx(std::sin(glm::radians(max_pitch_degrees))).epsilon(EPSILON));

		MoveMouse(0.0f, look_pixels * 2.0f);
		Frame(true, true);

		CHECK(Forward().y == doctest::Approx(-std::sin(glm::radians(max_pitch_degrees))).epsilon(EPSILON));
	};
};

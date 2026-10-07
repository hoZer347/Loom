#include "EditorCamera.h"

#include "RenderMath.h"

#include "glm/gtc/matrix_transform.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>


namespace Loom
{
	namespace
	{
		// Up and back from the origin, where a scene is built around, looking
		// down at it.
		constexpr glm::vec3 start_position(0.0f, 4.0f, 10.0f);

		// Far enough to take in a level, where a game's camera only needs its own.
		constexpr float far_plane = 1000.0f;

		constexpr float look_degrees_per_pixel = 0.2f;

		// Short of straight up or down, where yaw stops meaning anything.
		constexpr float max_pitch = 89.0f;

		constexpr float fly_units_per_second = 4.0f;
		constexpr float fast_multiplier = 4.0f;
		constexpr float pan_units_per_pixel = 0.01f;
		constexpr float dolly_units_per_notch = 0.5f;
	};

	EditorCamera::EditorCamera() :
		m_position(start_position),
		m_pitch(-glm::degrees(std::atan2(start_position.y, start_position.z)))
	{
		m_camera.farPlane = far_plane;
		m_camera.SetPose(glm::translate(glm::mat4(1.0f), m_position) * Rotation());
	};

	glm::mat4 EditorCamera::Rotation() const
	{
		const glm::mat4 yawed = glm::rotate(glm::mat4(1.0f), glm::radians(m_yaw), Y_AXIS);

		return glm::rotate(yawed, glm::radians(m_pitch), X_AXIS);
	};

	void EditorCamera::Drive(bool hovered, bool held)
	{
		const ImGuiIO& io = ImGui::GetIO();

		const glm::mat4 rotation = Rotation();
		const glm::vec3 right(rotation[0]);
		const glm::vec3 up(rotation[1]);
		const glm::vec3 forward = -glm::vec3(rotation[2]);

		if (held && ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			m_yaw -= io.MouseDelta.x * look_degrees_per_pixel;
			m_pitch = std::clamp(m_pitch - io.MouseDelta.y * look_degrees_per_pixel, -max_pitch, max_pitch);

			glm::vec3 direction(0.0f);

			if (ImGui::IsKeyDown(ImGuiKey_W)) direction += forward;
			if (ImGui::IsKeyDown(ImGuiKey_S)) direction -= forward;
			if (ImGui::IsKeyDown(ImGuiKey_D)) direction += right;
			if (ImGui::IsKeyDown(ImGuiKey_A)) direction -= right;
			if (ImGui::IsKeyDown(ImGuiKey_E)) direction += Y_AXIS;
			if (ImGui::IsKeyDown(ImGuiKey_Q)) direction -= Y_AXIS;

			const float speed = fly_units_per_second * (io.KeyShift ? fast_multiplier : 1.0f);

			m_position += direction * speed * io.DeltaTime;
		}
		else if (held && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			m_position += (up * io.MouseDelta.y - right * io.MouseDelta.x) * pan_units_per_pixel;

		if (hovered)
			m_position += forward * io.MouseWheel * dolly_units_per_notch;

		m_camera.SetPose(glm::translate(glm::mat4(1.0f), m_position) * Rotation());
	};
};

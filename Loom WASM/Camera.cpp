#include "Camera.h"

#include "RenderMath.h"
#include "Renderer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/euler_angles.hpp"


namespace Loom
{
	namespace
	{
		// How short the cross of forward and up may get before the two count as
		// parallel and another up stands in.
		constexpr float PARALLEL = 1e-4f;

		// How far ahead a target goes when the old one sat on the camera.
		constexpr float DEFAULT_TARGET_DISTANCE = 1.0f;

		// A zero scale leaves an axis with no direction.
		glm::vec3 Direction(const glm::vec3& v, const glm::vec3& fallback)
		{
			const float length = glm::length(v);

			return length > 0.0f
				? v / length
				: fallback;
		};
	};

	glm::mat4 Camera::ViewProjection(float aspect) const
	{
		return ViewProjection(GetAim(lookAtTarget), aspect);
	};

	glm::mat4 Camera::ViewProjection(const Aim& aim, float aspect) const
	{
		const glm::mat4 projection = glm::perspective(
			glm::radians((float)fieldOfView),
			aspect,
			(float)nearPlane,
			(float)farPlane);

		return projection * glm::lookAt(aim.eye, aim.eye + aim.forward, aim.up);
	};

	void Camera::Apply(uint32_t program) const
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		const glm::ivec2 size = renderer->TargetSize();

		if (size.x <= 0 || size.y <= 0)
			return;

		const float aspect = (float)size.x / (float)size.y;
		const Aim aim = GetAim(lookAtTarget);

		renderer->SetUniform(program, "u_viewProjection", ViewProjection(aim, aspect));
		renderer->SetUniform(program, "u_cameraPosition", aim.eye);
	};

	void Camera::SetLookAtTarget(bool on)
	{
		if (on == lookAtTarget)
			return;

		lookAtTarget = on;
		Reaim();
	};

	void Camera::OnFieldChanged(const SerializedField& field)
	{
		if (field.data == &*lookAtTarget)
			Reaim();
	};

	Camera::Aim Camera::GetAim(bool atTarget) const
	{
		const glm::mat4 world = m_gameObject
			? m_gameObject->WorldMatrix()
			: m_pose;

		Aim aim;
		aim.eye = glm::vec3(world[3]);
		aim.forward = Direction(-glm::vec3(world[2]), -Z_AXIS);
		aim.up = Direction(glm::vec3(world[1]), Y_AXIS);

		// A target on the eye has no direction, so the transform's stands.
		if (atTarget)
			aim.forward = Direction(*target - aim.eye, aim.forward);

		glm::vec3 right = glm::cross(aim.forward, aim.up);

		if (glm::length(right) < PARALLEL)
			right = glm::cross(aim.forward, UpFor(aim.forward));

		right = glm::normalize(right);
		aim.up = glm::cross(right, aim.forward);

		return aim;
	};

	void Camera::Reaim()
	{
		const Aim was = GetAim(!lookAtTarget);

		if (lookAtTarget)
		{
			const float distance = glm::length(*target - was.eye);
			const glm::vec3 ahead = was.eye + was.forward * (distance > 0.0f ? distance : DEFAULT_TARGET_DISTANCE);

			target = ahead;
			return;
		};

		if (!m_gameObject)
			return;

		// Rotation is relative to the parent, so the aim is taken back through
		// the whole of it: a parent scaled unevenly bends directions, and
		// undoing only its rotation would leave the bend in.
		glm::vec3 forward = was.forward;
		glm::vec3 up = was.up;

		if (const GameObject* parent = m_gameObject->GetParent())
		{
			const glm::mat3 matrix(parent->WorldMatrix());

			// Flattened by a zero scale, nothing under it can face anywhere.
			if (glm::determinant(matrix) == 0.0f)
				return;

			const glm::mat3 inverse = glm::inverse(matrix);

			forward = glm::normalize(inverse * forward);
			up = inverse * up;
			up = glm::normalize(up - glm::dot(up, forward) * forward);
		};

		const glm::mat3 local(glm::cross(forward, up), up, -forward);

		// LocalMatrix rotates by Ry * Rx * Rz, which this takes apart.
		glm::vec3 radians;
		glm::extractEulerAngleYXZ(glm::mat4(local), radians.y, radians.x, radians.z);

		const glm::vec3 degrees = glm::degrees(radians);
		m_gameObject->transform.rotation = degrees;
	};
};

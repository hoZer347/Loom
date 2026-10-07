#pragma once

#include "Loom API.h"

#include "Component.h"

#include "glm/glm.hpp"


namespace Loom
{
	/**
	* Loom::Camera
	* - Where the scene is seen from: its GameObject's position, looking down
	*   the transform's -Z with its +Y up, or at target when lookAtTarget is on.
	*   Looking at a target still takes its roll from the transform
	* - Scene::Render uses the first one in the hierarchy, unless it is handed
	*   one from outside the scene, and hands it to each mesh's program as it
	*   draws. The aspect ratio is the viewport's, so the same camera fits a
	*   window or an editor's framebuffer
	* - What a shader can declare, all optional:
	*     uniform mat4 u_viewProjection;     world space to clip space
	*     uniform vec3 u_cameraPosition;
	*/
	struct LOOM_API Camera : Component<Camera>
	{
		// World space to clip space, for a target of the given width over height.
		glm::mat4 ViewProjection(float aspect) const;

		void Apply(uint32_t program) const;

		// Where a camera on no GameObject sits, as a world matrix: the editor's
		// own cameras, which belong to no scene.
		void SetPose(const glm::mat4& world) { m_pose = world; };

		// Switches what the camera aims by, re-aiming the one it switches to so
		// the view does not turn: the target goes ahead of the camera at the
		// distance it was, or the transform is rotated to face it.
		void SetLookAtTarget(bool on);

		// The inspector's checkbox flips the field itself, so the re-aim follows.
		void OnFieldChanged(const SerializedField& field) override;

		// The camera the scene being rendered is seen through, for as long as
		// Scene::Render is drawing it.
		static inline Camera* current = nullptr;

		Serial<bool> lookAtTarget;
		// World space.
		Serial<glm::vec3> target;

		// Vertical, in degrees.
		Serial<float> fieldOfView = 45.0f;
		Serial<float> nearPlane = 0.1f;
		Serial<float> farPlane = 100.0f;

	private:
		struct Aim
		{
			glm::vec3 eye;
			glm::vec3 forward;
			glm::vec3 up;
		};

		// World space, orthonormal, aimed the way the flag given says.
		Aim GetAim(bool atTarget) const;

		glm::mat4 ViewProjection(const Aim& aim, float aspect) const;

		void Reaim();

		glm::mat4 m_pose{ 1.0f };
	};
};

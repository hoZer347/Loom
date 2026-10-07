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
	* - Scene::Render uses the first one in the hierarchy and hands it to each
	*   mesh's program as it draws. The aspect ratio is the viewport's, so the
	*   same camera fits a window or an editor's framebuffer
	* - What a shader can declare, all optional:
	*     uniform mat4 u_viewProjection;     world space to clip space
	*     uniform vec3 u_cameraPosition;
	*/
	struct LOOM_API Camera : Component<Camera>
	{
		// World space to clip space, for a target of the given width over height.
		glm::mat4 ViewProjection(float aspect) const;

		void Apply(uint32_t program) const;

		// Switches what the camera aims by, re-aiming the one it switches to so
		// the view does not turn: the target goes ahead of the camera at the
		// distance it was, or the transform is rotated to face it.
		void SetLookAtTarget(bool on);

		// The inspector's checkbox flips the field itself, so the re-aim follows.
		void OnFieldChanged(const SerializedField& field) override;

		// The camera the scene being rendered is seen through, for as long as
		// Scene::Render is drawing it.
		static inline Camera* current = nullptr;

		LOOM_SERIAL(bool, lookAtTarget);
		// World space.
		LOOM_SERIAL(Math::vec3<float>, target);

		// Vertical, in degrees.
		LOOM_SERIAL(float, fieldOfView, 45.0f);
		LOOM_SERIAL(float, nearPlane, 0.1f);
		LOOM_SERIAL(float, farPlane, 100.0f);

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
	};
};

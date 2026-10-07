#pragma once

#include "Camera.h"

#include "glm/glm.hpp"


namespace Loom
{
	/**
	* Loom::EditorCamera
	* - A camera that belongs to no scene, for the editor's views that look on
	*   from outside the game
	* - Flown the way Unity's Scene view is: hold the right mouse button to look
	*   around, and W, A, S and D with Q and E to fly while it is held, Shift to
	*   go faster. The middle button pans and the wheel moves in and out
	*/
	struct EditorCamera final
	{
		EditorCamera();

		// Takes this frame's mouse and keyboard from ImGui. hovered is whether
		// the view is under the mouse, held whether a press on it has not been
		// let go of yet, so a drag keeps steering once it leaves the view.
		void Drive(bool hovered, bool held);

		Camera* Get() { return &m_camera; };

	private:
		// The rotation alone, yaw about world up and then pitch about the
		// camera's own right.
		glm::mat4 Rotation() const;

		Camera m_camera;

		glm::vec3 m_position;

		// Degrees.
		float m_yaw = 0.0f;
		float m_pitch = 0.0f;
	};
};

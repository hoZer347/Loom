#pragma once

#include "EditorViewport.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::ModelPreview
	* - A model file turning on a turntable, so the Inspector can show what a
	*   file holds before it is imported
	* - Draws with a shader of its own, shaded by each face's slope, so the
	*   preview needs neither the project's shader nor a light in the scene
	*/
	struct ModelPreview final
	{
		// Reads the file, unless it is the one already shown and has not been
		// written to since. A file that cannot be read is not tried again until
		// it changes or another one has been shown.
		void Show(const std::string& path);

		// Draws the model turned by angle radians into a square of the given side.
		void Render(int size, float angle, const float* clear_colour);

		bool IsLoaded() const { return !m_vertices.empty(); };
		size_t GetMeshCount() const { return m_meshCount; };
		size_t GetTriangleCount() const;

		const EditorViewport& GetTarget() const { return m_target; };

	private:
		EditorViewport m_target;

		std::string m_path{ };
		std::filesystem::file_time_type m_writeTime{ };

		std::vector<float> m_vertices{ };
		size_t m_meshCount = 0;

		// Of the model's bounding sphere about the origin, which the camera frames.
		float m_radius = 0.0f;
	};
};

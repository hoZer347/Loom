#pragma once

#include <string>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::RenderingObjects
	* - What the Hierarchy's Rendering menu makes, and what a new scene starts with
	*/
	namespace RenderingObjects
	{
		constexpr const char* PreVertexLabel = "Pre-Vertex";
		constexpr const char* PixelShaderLabel = "Pixel Shader";
		constexpr const char* LightLabel = "Light";
		constexpr const char* CameraLabel = "Camera";
		constexpr const char* ComputeShaderLabel = "Compute Shader";

		// A child of parent with a PreVertex running file, which may be empty.
		GameObject* AddPreVertex(GameObject& parent, const std::string& file);

		// A child of parent with a PixelShader naming no file yet.
		GameObject* AddPixelShader(GameObject& parent);

		// A child of parent with a Light at its defaults.
		GameObject* AddLight(GameObject& parent);

		// A child of parent with a Camera at its defaults.
		GameObject* AddCamera(GameObject& parent);

		// What a new scene starts with: a light, and a camera looking down at a
		// plane drawn with shader, which may be empty.
		void AddStarter(GameObject& root, const std::string& shader);

		// A child of parent with a ComputeShader naming no file yet.
		GameObject* AddComputeShader(GameObject& parent);
	};
};

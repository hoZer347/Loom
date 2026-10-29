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

		// A child of parent with a PreVertex running file, which may be empty.
		GameObject* AddPreVertex(GameObject& parent, const std::string& file);

		// A child of parent with a PixelShader naming no file yet.
		GameObject* AddPixelShader(GameObject& parent);

		GameObject* AddLight(GameObject& parent);
		GameObject* AddCamera(GameObject& parent);

		// What a new scene starts with: a light, and a camera looking down at a
		// plane drawn with shader.
		void AddStarter(GameObject& root, const std::string& shader);
	};
};

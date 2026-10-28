#pragma once

#include <string>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::RenderingObjects
	* - What the Hierarchy's Rendering menu makes
	*/
	namespace RenderingObjects
	{
		constexpr const char* PreVertexLabel = "Pre-Vertex";
		constexpr const char* PixelShaderLabel = "Pixel Shader";

		// A child of parent with a PreVertex running file, which may be empty.
		GameObject* AddPreVertex(GameObject& parent, const std::string& file);

		// A child of parent with a PixelShader naming no file yet.
		GameObject* AddPixelShader(GameObject& parent);
	};
};

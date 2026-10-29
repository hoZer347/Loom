#pragma once

#include "EditableMesh.h"

#include <span>
#include <string>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::Shapes
	* - The shapes the editor makes from nothing: the Model panel's + Add, and
	*   the Hierarchy's 3D Object menu
	*/
	namespace Shapes
	{
		enum class Shape
		{
			Cube,
			Plane,
			Cylinder,
			Cone,
			Sphere,
			Capsule,
		};

		struct Info
		{
			Shape shape;
			const char* label;
		};

		// Every shape, in the order menus list them.
		std::span<const Info> All();

		const char* Label(Shape shape);

		// Centred on the origin and size across, the capsule twice as tall as
		// it is across.
		EditableMesh Build(Shape shape, float size);

		// A child of parent named for the shape, drawing it one unit across
		// with the shader.
		GameObject* Add(GameObject& parent, Shape shape, const std::string& shader);
	};
};

#include "Shapes.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"

#include <algorithm>
#include <iterator>
#include <map>


namespace Loom::Shapes
{
	namespace
	{
		// Sides around a round shape, and rings from pole to pole of a sphere.
		constexpr int ROUND_SEGMENTS = 16;
		constexpr int SPHERE_RINGS = 8;

		// How big Add makes a shape.
		constexpr float ADDED_SIZE = 1.0f;

		// A capsule's height over its width.
		constexpr float CAPSULE_ASPECT = 2.0f;

		constexpr Info SHAPES[] =
		{
			{ Shape::Cube, "Cube" },
			{ Shape::Plane, "Plane" },
			{ Shape::Cylinder, "Cylinder" },
			{ Shape::Cone, "Cone" },
			{ Shape::Sphere, "Sphere" },
			{ Shape::Capsule, "Capsule" },
		};

		void CountShaders(GameObject& object, std::map<std::string, int>& counts)
		{
			if (const Material* material = object.GetComponent<Material>(); material && !material->GetShaderPath().empty())
				counts[material->GetShaderPath()]++;

			for (GameObject* child : object.GetChildren())
				CountShaders(*child, counts);
		};
	};

	std::span<const Info> All()
	{
		return SHAPES;
	};

	const char* Label(Shape shape)
	{
		return std::find_if(std::begin(SHAPES), std::end(SHAPES), [shape](const Info& each) { return each.shape == shape; })->label;
	};

	EditableMesh Build(Shape shape, float size)
	{
		const float radius = size / 2.0f;

		switch (shape)
		{
		case Shape::Cube:		return EditableMesh::Cube(size);
		case Shape::Plane:		return EditableMesh::Plane(size);
		case Shape::Cylinder:	return EditableMesh::Cylinder(radius, size, ROUND_SEGMENTS);
		case Shape::Cone:		return EditableMesh::Cone(radius, size, ROUND_SEGMENTS);
		case Shape::Sphere:		return EditableMesh::Sphere(radius, ROUND_SEGMENTS, SPHERE_RINGS);
		case Shape::Capsule:	return EditableMesh::Capsule(radius, size * CAPSULE_ASPECT, ROUND_SEGMENTS, SPHERE_RINGS / 2);
		};

		return { };
	};

	GameObject* Add(GameObject& parent, Shape shape, const std::string& shader)
	{
		GameObject* object = parent.AddChild(Label(shape));
		object->Attach<Material>()->SetShaderPath(shader);
		Build(shape, ADDED_SIZE).WriteTo(*object->Attach<Mesh>(Mesh::Triangles));

		return object;
	};

	std::string MostUsedShader(const std::vector<GameObject*>& roots)
	{
		std::map<std::string, int> counts{ };

		for (GameObject* root : roots)
			CountShaders(*root, counts);

		// The first of the largest, so a tie goes to the first path.
		const auto most = std::max_element(
			counts.begin(),
			counts.end(),
			[](const auto& a, const auto& b) { return a.second < b.second; });

		return most == counts.end() ? std::string() : most->first;
	};
};

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <vector>


namespace Loom
{
	struct ComponentBase;
	struct GameObject;
	struct LoomObject;

	/**
	* Loom::MultiEdit
	* - Several GameObjects selected in the Hierarchy and edited in the
	*   Inspector at once, the first of them the one it is drawn from
	* - A field whose objects disagree shows as mixed, per axis for a vector,
	*   and an edit to the first is written into the rest, only the axes it
	*   changed for a vector, or moves the rest by as much when it was dragged
	*/
	namespace MultiEdit
	{
		#pragma region Selection

		// The most axes a vector field has.
		inline constexpr size_t max_axes = 4;

		using Axes = std::array<bool, max_axes>;

		// The rows under root, root's own not among them, in the order the
		// Hierarchy draws them, going into a node only when open says it is.
		std::vector<GameObject*> Rows(GameObject& root, const std::function<bool(const GameObject&)>& open);

		// The rows from one of a and b to the other, either way round. Empty
		// when either is not a row.
		std::vector<GameObject*> Range(const std::vector<GameObject*>& rows, const GameObject* a, const GameObject* b);

		// The objects with none of their ancestors among them, so a delete or
		// a copy takes each subtree once.
		std::vector<GameObject*> Tops(const std::vector<GameObject*>& objects);

		#pragma endregion

		#pragma region Components

		// The first object's components that every other object has too, each
		// with its match on every object: the same type, as many of that type
		// along. One that any object lacks is left out.
		std::vector<std::vector<ComponentBase*>> Common(const std::vector<GameObject*>& objects);

		#pragma endregion

		#pragma region Fields

		// The number of axes a field of the type at index has, or 0 when it is
		// not a vector.
		size_t AxisCount(const LoomObject& object, size_t index);

		// Whether the objects' fields at index do not all read the same.
		bool Mixed(const std::vector<LoomObject*>& objects, size_t index);

		// Per axis of the vector field at index, whether the objects disagree.
		Axes MixedAxes(const std::vector<LoomObject*>& objects, size_t index);

		// Writes the first object's field at index into the rest. A vector
		// writes only the axes where the first now differs from before.
		void Spread(const std::vector<LoomObject*>& objects, size_t index, const float* before = nullptr);

		// Moves the rest's vector field at index by as much as the first's
		// moved from before, so each keeps its distance from the first.
		void Shift(const std::vector<LoomObject*>& objects, size_t index, const float* before);

		#pragma endregion
	};
};

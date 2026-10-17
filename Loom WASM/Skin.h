#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Guid.h"

#include "glm/glm.hpp"

#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	struct Mesh;

	// The GameObjects under root, by name, root included. The first of a name wins.
	LOOM_API std::unordered_map<std::string, GameObject*> DescendantsByName(GameObject& root);

	// One name a line, as Skin and Animator keep their lists of names.
	LOOM_API std::vector<std::string> SplitLines(const std::string& text);

	/**
	* Loom::NamedObjects
	* - The GameObjects called by a list of names under a root, remembered by
	*   guid, so finding them again is a lookup rather than a walk of the
	*   hierarchy and one destroyed since is noticed rather than read
	* - Walks the hierarchy again when the root or the names change, or one it
	*   found is gone
	*/
	struct LOOM_API NamedObjects final
	{
		// The objects in the order of names, null for a name nothing under root has.
		const std::vector<GameObject*>& Find(GameObject* root, const std::vector<std::string>& names);

	private:
		GameObject* m_root = nullptr;
		std::vector<std::string> m_names;
		std::vector<Guid> m_guids;
		std::vector<GameObject*> m_found;
	};

	// A list of names kept as one name a line, split again only when the text changes.
	struct LOOM_API NameList final
	{
		const std::vector<std::string>& Of(const std::string& text);

	private:
		std::string m_text;
		std::vector<std::string> m_names;
	};

	/**
	* Loom::Skin
	* - Bends its GameObject's Mesh the way a model's skeleton bends it: each
	*   vertex follows up to four joints, by weight, from where they were when
	*   the model was bound to them
	* - A joint is a GameObject under skeleton, found by name, so moving one -
	*   by hand, by a script or by an Animator - moves the vertices bound to it.
	*   A joint that is not there holds its vertices where they were bound
	* - Its shape keys each move some vertices by an offset, scaled by the
	*   key's weight, before the joints bend them
	* - The Mesh's own vertices are never changed: they are bent again, from
	*   where the model was bound, every time the Mesh draws
	*/
	struct LOOM_API Skin : Component<Skin, "Rendering">
	{
		static constexpr size_t JOINTS_PER_VERTEX = 4;

		// A vertex's joint indices, then their weights.
		static constexpr size_t FLOATS_PER_INFLUENCE = 2 * JOINTS_PER_VERTEX;

		// A shape key's moved vertex: its index, then its offset.
		static constexpr size_t FLOATS_PER_OFFSET = 4;

		static constexpr size_t MATRIX_FLOATS = 16;

		Skin();

		// Fills the mesh's m_deformed with its vertices moved by the shape keys
		// and bent by the joints.
		void Deform(Mesh& mesh);

		// 0 leaves a shape key's vertices where they are; 1 moves them all the
		// way. A name the skin has no key for is ignored.
		void SetShapeWeight(const std::string& name, float weight);

		// 0 for a name the skin has no key for.
		float GetShapeWeight(const std::string& name);

		std::vector<std::string> GetShapeNames();

		// A slider for each shape key.
		void OnGui() override;

		// Where the joints are looked for. Usually the model the skin came in.
		Serial<GameObject*> skeleton;

		// One joint name a line, in the order m_influences and m_inverseBinds
		// count them.
		Serial<std::string> m_joints;

		// Each joint's inverse bind matrix, column by column: from the mesh's
		// space into the joint's, as they were when the model was bound.
		Serial<std::vector<float>> m_inverseBinds;

		// For each vertex, its four joint indices, then their four weights.
		Serial<std::vector<float>> m_influences;

		// One shape key name a line.
		Serial<std::string> m_shapes;

		// For each shape key in turn, how many vertices it moves, then each of
		// them: its index and its offset.
		Serial<std::vector<float>> m_shapeOffsets;

		// Each shape key's weight, in the order m_shapes names them.
		Serial<std::vector<float>> m_shapeWeights;

	private:
		// Where each shape key's run of offsets starts in m_shapeOffsets.
		std::vector<size_t> ShapeStarts() const;

		// From the mesh's space, bent by each joint as it now is.
		std::vector<glm::mat4> JointMatrices(const Mesh& mesh);

		NameList m_jointNames;
		NameList m_shapeNames;
		NamedObjects m_jointObjects;
	};
};

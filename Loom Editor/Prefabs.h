#pragma once

#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct Prefab;

	/**
	* Loom::Prefabs
	* - Keeps a prefab's instances and its .loomprefab file alike, working on
	*   the text SceneSerializer writes a GameObject as, parsed into a tree
	* - An instance shares everything with its prefab but its own identities:
	*   the guids of what is in it, the transform of every object in it, the
	*   name of its top object, the Prefab component linking it, and
	*   references out of it, which can only mean something where it is
	* - Objects and components are matched by where they sit: the same child of
	*   the same parent, the same component of the same object
	*/
	namespace Prefabs
	{
		#pragma region Nodes

		struct Field
		{
			std::string key;
			std::string value;
		};

		struct ComponentNode
		{
			std::string type;
			std::string guid;
			std::vector<Field> fields;
		};

		struct ObjectNode
		{
			std::string name;
			std::string guid;
			std::vector<Field> fields;
			std::vector<ComponentNode> components;
			std::vector<ObjectNode> children;
		};

		#pragma endregion

		#pragma region Links

		// The keys a GameObject's transform fields are written under.
		std::vector<std::string> TransformKeys(const GameObject& gameObject);

		// The registered name the Prefab component is written under.
		const std::string& PrefabType();

		// The Prefab component linking a GameObject to its prefab, or null when
		// it has none or one naming no file.
		Prefab* LinkOf(const GameObject& gameObject);

		// The link of the prefab gameObject is part of: its own, or its nearest
		// ancestor's. Null when it is in none.
		Prefab* NearestLink(const GameObject& gameObject);

		#pragma endregion

		#pragma region Text

		// What SceneSerializer::Serialize(GameObject&) writes, and back. Parse
		// is false, with why in error, for text that is not one GameObject.
		bool Parse(const std::string& text, ObjectNode& root, std::string* error = nullptr);
		std::string Write(const ObjectNode& root);

		// Scene text with the lines of the GameObject with this guid, and of
		// everything under it, swapped for subtree written at its depth. The
		// text unchanged when no GameObject has the guid.
		std::string Splice(const std::string& scene, const std::string& guid, const std::string& subtree);

		#pragma endregion

		#pragma region Syncing

		// What a prefab and the trees compared with it agree on: Shared for an
		// instance, Whole for the Prefab panel's copy, whose transforms and
		// name are the file's.
		enum class Part
		{
			Shared,
			Whole
		};

		// One instance of a prefab, and whether it agreed with the prefab when
		// last looked at: one that did and no longer does has been edited, and
		// one that never did takes the prefab as it is.
		struct Instance
		{
			ObjectNode tree;
			bool known = false;
		};

		// What a sync changes: the prefab, when something edited it, each
		// instance that has to take it, and whether the Prefab panel has to
		// show it again, an instance having edited it, or show its own unsaved
		// edit with the instance's carried into it.
		struct Outcome
		{
			std::optional<ObjectNode> prefab;
			std::vector<std::optional<ObjectNode>> instances;
			bool reshow = false;
			std::optional<ObjectNode> panel;
		};

		// The same text for two trees exactly when they agree on part.
		std::string Shape(const ObjectNode& root, const std::vector<std::string>& transform_keys, Part part);

		// source as target would be with part taken from it: target's guids,
		// fresh ones for anything target has no counterpart to, and references
		// within source pointed at their counterparts. The top object's Prefab
		// components are target's.
		ObjectNode Retarget(
			const ObjectNode& source,
			const ObjectNode& target,
			const std::vector<std::string>& transform_keys,
			Part part);

		// The tree with every reference out of it emptied, as a prefab keeps
		// it: there is nothing outside a prefab to point at.
		ObjectNode Isolated(const ObjectNode& root);

		// ours with what theirs changed from base, all three the same prefab:
		// ours' guids, and ours' wherever both changed the same thing.
		ObjectNode Merge(
			const ObjectNode& base,
			const ObjectNode& ours,
			const ObjectNode& theirs,
			const std::vector<std::string>& transform_keys);

		// One pass of keeping the prefab and its instances alike. The panel is
		// the Prefab panel's copy, when it shows this prefab, with the Whole
		// shape it last agreed with the file at. An edit there wins when saved,
		// then the first known instance's, and everything else takes the
		// result. An unsaved edit stays in the panel, with an instance's carried
		// into it.
		Outcome Sync(
			const ObjectNode& prefab,
			const ObjectNode* panel,
			const std::string& panel_shape,
			const std::vector<Instance>& instances,
			const std::vector<std::string>& transform_keys,
			bool save = true);

		#pragma endregion
	};
};

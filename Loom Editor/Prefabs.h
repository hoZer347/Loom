#pragma once

#include <string>
#include <vector>


namespace Loom
{
	struct GameObject;

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

		// What a prefab and the trees compared with it agree on: Shared for an
		// instance, Whole for the prefab viewer's copy, whose transforms and
		// name are the file's.
		enum class Part
		{
			Shared,
			Whole
		};

		// The keys a GameObject's transform fields are written under.
		std::vector<std::string> TransformKeys(const GameObject& gameObject);

		// The registered name the Prefab component is written under.
		const std::string& PrefabType();

		// What SceneSerializer::Serialize(GameObject&) writes, and back. Parse
		// is false, with why in error, for text that is not one GameObject.
		bool Parse(const std::string& text, ObjectNode& root, std::string* error = nullptr);
		std::string Write(const ObjectNode& root);

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

		// Scene text with the lines of the GameObject with this guid, and of
		// everything under it, swapped for subtree written at its depth. The
		// text unchanged when no GameObject has the guid.
		std::string Splice(const std::string& scene, const std::string& guid, const std::string& subtree);
	};
};

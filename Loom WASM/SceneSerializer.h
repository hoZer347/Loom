#pragma once

#include "Loom API.h"

#include "Guid.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>


namespace Loom
{
	struct Scene;
	struct GameObject;
	struct SerializedField;

	/**
	* Loom::SceneSerializer
	* - Reads and writes the scene format:
	*
	*		<scene name>(<guid>):
	*			GameObject <name>(<guid>)
	*				<field> = <value>
	*				Component <type>(<guid>)
	*					<field> = <value>
	*				GameObject <child name>(<guid>)
	*
	* - One tab per level of nesting. A line's depth says what owns it: fields
	*   belong to the object one level up, components to the GameObject one level
	*   up, and a GameObject nested under a GameObject is its child
	* - Objects are written with the guid they are carrying, and references
	*   between them are written as those guids, so a scene read back has the
	*   same shape and the same internal links as the one that was written
	*/
	struct LOOM_API SceneSerializer final
	{
		static constexpr const char* extension = ".loomscene";

		// What starts a GameObject's line and a component's.
		static constexpr const char* gameobject_keyword = "GameObject ";
		static constexpr const char* component_keyword = "Component ";

		// Tabs are what the format uses; four spaces are accepted as one so a
		// file that has been through an editor that expands tabs still loads.
		static constexpr int spaces_per_tab = 4;

		// A line's depth, and where what follows its indent starts.
		static int DepthOf(const std::string& line, size_t& content_start);

		// "Player(0f8e...)" -> name "Player", guid parsed. The last bracket wins,
		// so a name is free to contain brackets of its own.
		static bool SplitNameAndGuid(const std::string& text, std::string& name, Guid& guid);

		// References written as guids whose objects did not exist yet when the
		// line naming them was read.
		using PendingReferences = std::vector<std::pair<const SerializedField*, Guid>>;

		static std::string Serialize(Scene& scene);

		// Writes the scene to a file, creating the directories leading to it.
		static bool SaveToFile(Scene& scene, const std::string& path, std::string* error = nullptr);

		// Builds a new scene from the text. Returns null and fills in error when
		// the text is not a scene; anything smaller (a component this build does
		// not know, a field that no longer exists, a reference to something that
		// was never written) is reported to std::cerr and skipped, so an
		// out-of-date file still loads as far as it can.
		//
		// Given pending, the references the text leaves unresolved are added to
		// it instead of being resolved at the end, so scenes that point into each
		// other can all be built before ResolveReferences links them.
		static Scene* Deserialize(
			const std::string& text,
			std::string* error = nullptr,
			PendingReferences* pending = nullptr);

		static void ResolveReferences(const PendingReferences& pending);

		// A scene read a slice at a time, for a caller that draws frames while
		// it loads. Deserialize is one of these read to the end.
		struct LOOM_API Loading final
		{
			// Text with nothing in it is an empty scene called blank_name, or
			// with no blank_name, not a scene.
			Loading(std::string text, std::string blank_name);
			~Loading();

			Loading(const Loading&) = delete;
			Loading& operator=(const Loading&) = delete;

			// Reads on for about that many seconds, at least a line. False once
			// there is nothing left to read or the text has turned out not to
			// be a scene.
			bool Step(double seconds);

			// How far through the text the reading is, from 0 to 1.
			[[nodiscard]] float Progress() const;

			// Reads whatever is left and hands the scene over, as Deserialize
			// does. Once only. A Loading dropped before this takes what it has
			// built so far with it.
			Scene* Finish(std::string* error = nullptr, PendingReferences* pending = nullptr);

		private:
			friend struct SceneSerializer;

			struct State;

			// Reads text the caller keeps alive until Finish, with no copy.
			explicit Loading(const std::string& text);

			std::unique_ptr<State> m_state;
		};

		// The file's text, ready to be read a slice at a time. Null, with error
		// filled in, when the file cannot be opened. A file with nothing in it
		// is an empty scene named after the file.
		static std::unique_ptr<Loading> BeginLoadFromFile(const std::string& path, std::string* error = nullptr);

		// BeginLoadFromFile read to the end in one go.
		static Scene* LoadFromFile(const std::string& path, std::string* error = nullptr);

		// One GameObject and everything under it, as a scene writes it. Nothing
		// above it is written, so the text is what a clipboard holds.
		static std::string Serialize(GameObject& gameObject);

		// Builds the GameObjects in that text under parent and returns the first.
		// Each gets a new guid, so the same text can go in any number of times,
		// and references between them follow them to their new guids.
		static GameObject* Deserialize(const std::string& text, GameObject& parent, std::string* error = nullptr);

		// One GameObject without its children: its line, its fields and its
		// components, at the top level, as a scene writes it.
		static std::string SerializeOwn(GameObject& gameObject);

		// The scene's header and fields, without its root.
		static std::string SerializeHeader(Scene& scene);

		// Puts what SerializeOwn wrote back onto gameObject in place: its name,
		// the fields that read differently, and its components, kept by guid
		// and type, made, or detached and put in order. Every reference the
		// text names by guid is left in pending, for the caller to resolve as
		// it sees fit, and is OnFieldChanged's to report once it does; the
		// other fields read are reported here. False when the text is not one
		// GameObject.
		static bool ReadOwn(GameObject& gameObject, const std::string& text, PendingReferences& pending);

		// As ReadOwn, what SerializeHeader wrote back onto scene.
		static bool ReadHeader(Scene& scene, const std::string& text, PendingReferences& pending);
	};
};

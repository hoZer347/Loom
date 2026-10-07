#include "SceneSerializer.h"

#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Scene.h"

#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace
	{
		const char* const gameobject_keyword = "GameObject ";
		const char* const component_keyword = "Component ";

		std::string Indent(int depth)
		{
			return std::string(depth, '\t');
		};

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");

			if (first == std::string::npos)
				return "";

			return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
		};

		// The key on a field line: the position of the Serial member it belongs
		// to, and nothing else, so a typo is a bad file rather than a value
		// quietly landing on field zero.
		bool Index(const std::string& text, size_t& index)
		{
			// No object has ten thousand fields, and a longer run of digits than
			// that wraps size_t back into the range of one that does: 2^64 comes
			// out as field zero.
			if (text.empty() || text.size() > 4)
				return false;

			index = 0;

			for (const char c : text)
			{
				if (c < '0' || c > '9')
					return false;

				index = index * 10 + (size_t)(c - '0');
			};

			return true;
		};

		// "Player(0f8e...)" -> name "Player", guid parsed. The last bracket wins,
		// so a name is free to contain brackets of its own.
		bool SplitNameAndGuid(const std::string& text, std::string& name, Guid& guid)
		{
			const size_t open = text.find_last_of('(');
			const size_t close = text.find_last_of(')');

			if (open == std::string::npos || close == std::string::npos || close < open)
				return false;

			name = Trim(text.substr(0, open));

			return Guid::TryParse(text.substr(open + 1, close - open - 1), guid);
		};

		// A field has no name: what identifies it is where its Serial member was
		// declared, so that is what goes in the file.
		void WriteFields(std::string& text, const LoomObject& object, int depth)
		{
			const std::vector<SerializedField>& fields = object.GetFields();

			for (size_t i = 0; i < fields.size(); i++)
				text += Indent(depth) + std::to_string(i) + " = " + fields[i].Write() + '\n';
		};

		void WriteGameObject(std::string& text, GameObject& gameObject, int depth)
		{
			text +=
				Indent(depth) + gameobject_keyword +
				gameObject.GetName() + '(' + gameObject.GetGuid().ToString() + ")\n";

			WriteFields(text, gameObject, depth + 1);

			for (ComponentBase* component : gameObject.GetComponents())
			{
				text +=
					Indent(depth + 1) + component_keyword +
					ComponentRegistry::NameOf(*component) +
					'(' + component->GetGuid().ToString() + ")\n";

				WriteFields(text, *component, depth + 2);
			};

			for (GameObject* child : gameObject.GetChildren())
				WriteGameObject(text, *child, depth + 1);
		};

		// Tabs are what the format uses; four spaces are accepted as one so a
		// file that has been through an editor that expands tabs still loads.
		int DepthOf(const std::string& line, size_t& content_start)
		{
			int depth = 0;
			size_t at = 0;
			int spaces = 0;

			while (at < line.size())
			{
				if (line[at] == '\t')
				{
					depth++;
					spaces = 0;
				}
				else if (line[at] == ' ')
				{
					if (++spaces == 4)
					{
						depth++;
						spaces = 0;
					};
				}
				else break;

				at++;
			};

			content_start = at;

			return depth;
		};

		// A component has no name of its own, so the type is what identifies it
		// in a message about one.
		std::string Describe(LoomObject& object)
		{
			ComponentBase* component = dynamic_cast<ComponentBase*>(&object);

			return component
				? ComponentRegistry::NameOf(*component)
				: object.GetName();
		};

		// One entry per indentation level: what that level introduced, so the
		// next line down knows what it belongs to.
		struct Level
		{
			LoomObject* object = nullptr;
			GameObject* gameObject = nullptr;
		};

		// Hands each line that carries something to read, with how deep it sits.
		// Stops at the first line read turns down.
		bool ReadLines(
			const std::string& text,
			int& line_number,
			const std::function<bool(int depth, const std::string& content)>& read)
		{
			std::stringstream lines(text);
			std::string line;

			line_number = 0;

			while (std::getline(lines, line))
			{
				line_number++;

				size_t content_start = 0;
				const int depth = DepthOf(line, content_start);
				const std::string content = Trim(line.substr(content_start));

				if (content.empty() || content[0] == '#')
					continue;

				if (!read(depth, content))
					return false;
			};

			return true;
		};

		// Builds objects from the lines under a header. A scene and a pasted
		// subtree differ only in what a GameObject one level down becomes.
		struct Reader
		{
			std::vector<Level> levels{ };

			std::function<GameObject*(const std::string& name)> top_level;

			// Pasted objects are handed new guids, so a second paste of the same
			// text does not take the first one's identity. A reference into the
			// pasted objects follows them to their new guid.
			std::unordered_map<Guid, Guid> fresh{ };

			std::string error{ };

			// References are written as guids, and the object a guid names may
			// not have been built yet when the line mentioning it is read.
			SceneSerializer::PendingReferences pending{ };

			bool Read(int depth, const std::string& content)
			{
				const auto fail =
					[&](const std::string& reason)
					{
						error = reason;
						return false;
					};

				if (depth < 1)
					return fail("only one scene per file");

				if ((size_t)depth > levels.size())
					return fail("indented past its parent");

				levels.resize(depth + 1);

				const Level& parent = levels[depth - 1];

				if (content.rfind(gameobject_keyword, 0) == 0)
				{
					std::string name;
					Guid guid;

					if (!SplitNameAndGuid(content.substr(strlen(gameobject_keyword)), name, guid))
						return fail("GameObject is missing a valid guid");

					GameObject* gameObject = nullptr;

					if (depth == 1)
						gameObject = top_level(name);
					else if (parent.gameObject)
						gameObject = parent.gameObject->AddChild(name);
					else return fail("a GameObject has to sit under another GameObject");

					gameObject->SetName(name);
					gameObject->SetGuid(Identity(guid));

					levels[depth] = Level{ gameObject, gameObject };

					return true;
				};

				if (content.rfind(component_keyword, 0) == 0)
				{
					std::string name;
					Guid guid;

					if (!SplitNameAndGuid(content.substr(strlen(component_keyword)), name, guid))
						return fail("Component is missing a valid guid");

					if (parent.gameObject == nullptr)
						return fail("a component has to sit under a GameObject");

					ComponentBase* component = ComponentRegistry::Create(name, *parent.gameObject);

					if (component == nullptr)
					{
						std::cerr
							<< "Scene load: no component type named '" << name
							<< "' is registered, skipping it" << std::endl;

						// Left null on purpose: the fields underneath it then have
						// nowhere to go and are skipped with them.
						levels[depth] = Level{ };

						return true;
					};

					component->SetGuid(Identity(guid));

					levels[depth] = Level{ component, nullptr };

					return true;
				};

				// Anything else is a field of whatever is one level up.
				const size_t equals = content.find('=');

				if (equals == std::string::npos)
					return fail("expected 'GameObject', 'Component' or '<index> = <value>'");

				if (parent.object == nullptr)
					return true;

				const std::string key = Trim(content.substr(0, equals));
				std::string value = Trim(content.substr(equals + 1));

				size_t index = 0;

				// Not an index at all: a typo, or a file from when fields were keyed
				// by member name. Skipped like a field the type no longer has, so
				// the rest of the scene still loads.
				if (!Index(key, index))
				{
					std::cerr
						<< "Scene load: " << Describe(*parent.object)
						<< " has no field called '" << key << "', skipping it" << std::endl;

					return true;
				};

				const std::vector<SerializedField>& fields = parent.object->GetFields();

				// A type that has lost a field since this was written, or never had
				// one this far along: the line has nowhere to go.
				if (index >= fields.size())
				{
					std::cerr
						<< "Scene load: " << Describe(*parent.object)
						<< " has no field " << index << ", skipping it" << std::endl;

					return true;
				};

				const SerializedField* field = &fields[index];

				Guid guid;
				const bool names_guid =
					field->type == FieldType::Reference && Guid::TryParse(value, guid);

				if (names_guid)
					value = Identity(guid).ToString();

				if (field->Read(value))
					return true;

				// A reference that did not resolve is not a bad line; the object it
				// names may still be further down the text.
				if (names_guid)
				{
					pending.emplace_back(field, Identity(guid));
					return true;
				};

				std::cerr
					<< "Scene load: could not read '" << key
					<< " = " << value << "'" << std::endl;

				return true;
			};

		private:
			Guid Identity(const Guid& guid) const
			{
				const auto found = fresh.find(guid);

				return found == fresh.end()
					? guid
					: found->second;
			};
		};
	};

	std::string SceneSerializer::Serialize(Scene& scene)
	{
		std::string text =
			scene.GetName() + '(' + scene.GetGuid().ToString() + "):\n";

		WriteFields(text, scene, 1);

		WriteGameObject(text, scene.GetRoot(), 1);

		return text;
	};

	std::string SceneSerializer::Serialize(GameObject& gameObject)
	{
		std::string text;

		WriteGameObject(text, gameObject, 0);

		return text;
	};

	bool SceneSerializer::SaveToFile(Scene& scene, const std::string& path, std::string* error)
	{
		const std::filesystem::path file(path);

		std::error_code code;

		if (file.has_parent_path())
			std::filesystem::create_directories(file.parent_path(), code);

		std::ofstream out(file);

		if (!out)
		{
			if (error)
				*error = "Could not open '" + path + "' for writing";

			return false;
		};

		out << Serialize(scene);

		return true;
	};

	Scene* SceneSerializer::LoadFromFile(const std::string& path, std::string* error)
	{
		std::ifstream in(path);

		if (!in)
		{
			if (error)
				*error = "Could not open '" + path + "'";

			return nullptr;
		};

		std::stringstream buffer;
		buffer << in.rdbuf();

		return Deserialize(buffer.str(), error);
	};

	Scene* SceneSerializer::Deserialize(
		const std::string& text,
		std::string* error,
		PendingReferences* pending_out)
	{
		Scene* scene = nullptr;
		bool root_claimed = false;
		Reader reader;

		int line_number = 0;
		std::string reason;

		const bool read = ReadLines(
			text,
			line_number,
			[&](int depth, const std::string& content)
			{
				if (scene)
				{
					if (reader.Read(depth, content))
						return true;

					reason = reader.error;
					return false;
				};

				// The header names the scene and hands it its identity back.
				std::string name;
				Guid guid;

				if (depth != 0 || content.back() != ':')
					reason = "expected a scene header, '<name>(<guid>):'";
				else if (!SplitNameAndGuid(content.substr(0, content.size() - 1), name, guid))
					reason = "scene header is missing a valid guid";
				else
				{
					scene = new Scene(name);
					scene->SetGuid(guid);

					reader.levels.assign(1, Level{ scene, nullptr });

					// The first GameObject in the file is the scene's own root,
					// which already exists; everything else hangs off one.
					reader.top_level =
						[scene, &root_claimed](const std::string& child)
						{
							if (root_claimed)
								return scene->GetRoot().AddChild(child);

							root_claimed = true;

							return &scene->GetRoot();
						};
				};

				return reason.empty();
			});

		if (read && scene == nullptr)
			reason = "the file is empty";

		if (!reason.empty())
		{
			if (error)
				*error = "line " + std::to_string(line_number) + ": " + reason;

			// Everything built so far has work queued against it: a child
			// waiting to be linked to its parent, a component waiting for
			// OnAttach. That work has to land before the objects go, or it
			// lands on freed memory the next time the queue is drained.
			Engine::DoTasks();

			delete scene;

			Engine::DoTasks();

			return nullptr;
		};

		if (pending_out)
			pending_out->insert(pending_out->end(), reader.pending.begin(), reader.pending.end());
		else ResolveReferences(reader.pending);

		return scene;
	};

	void SceneSerializer::ResolveReferences(const PendingReferences& pending)
	{
		for (const auto& [field, guid] : pending)
		{
			LoomObject* target = LoomObject::GetByGuid(guid);

			if (target == nullptr)
			{
				std::cerr
					<< "Scene load: a reference points at " << guid.ToString()
					<< ", which is not loaded" << std::endl;

				continue;
			};

			field->SetReference(target);

			if (field->GetReference() == nullptr)
				std::cerr
					<< "Scene load: a reference cannot point at " << guid.ToString()
					<< ", which is not the type it holds" << std::endl;
		};
	};

	GameObject* SceneSerializer::Deserialize(const std::string& text, GameObject& parent, std::string* error)
	{
		std::vector<GameObject*> built{ };
		Reader reader;

		// The text's top level sits one under the parent, which is not the
		// text's to put fields on.
		reader.levels.assign(1, Level{ });

		reader.top_level =
			[&](const std::string& name)
			{
				built.push_back(parent.AddChild(name));
				return built.back();
			};

		int line_number = 0;

		// Every new guid is decided before the first line is read, since a
		// reference can name an object further down.
		ReadLines(
			text,
			line_number,
			[&](int, const std::string& content)
			{
				for (const char* keyword : { gameobject_keyword, component_keyword })
				{
					std::string name;
					Guid guid;

					if (content.rfind(keyword, 0) == 0 &&
						SplitNameAndGuid(content.substr(strlen(keyword)), name, guid))
						reader.fresh[guid] = Guid::New();
				};

				return true;
			});

		const bool read = ReadLines(
			text,
			line_number,
			[&](int depth, const std::string& content)
			{
				return reader.Read(depth + 1, content);
			});

		if (read && !built.empty())
		{
			SceneSerializer::ResolveReferences(reader.pending);
			return built.front();
		};

		if (error)
			*error = read
				? std::string("there is no GameObject in it")
				: "line " + std::to_string(line_number) + ": " + reader.error;

		// Queued behind the links AddChild queued, so each lands on a child
		// that is in place.
		for (GameObject* gameObject : built)
			gameObject->Destroy();

		return nullptr;
	};
};

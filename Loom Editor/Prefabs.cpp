#include "Prefabs.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Guid.h"
#include "Prefab.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>


namespace Loom
{
	namespace
	{
		#pragma region Text

		constexpr const char* field_separator = " = ";
		constexpr const char* null_reference = "null";

		std::string Indent(int depth)
		{
			return std::string((size_t)depth, '\t');
		};

		bool StartsWith(const std::string& text, const char* prefix)
		{
			return text.rfind(prefix, 0) == 0;
		};

		bool IsGuid(const std::string& text)
		{
			Guid guid;

			return Guid::TryParse(text, guid);
		};

		bool SplitNameAndGuid(const std::string& text, std::string& name, std::string& guid)
		{
			Guid parsed;

			if (!SceneSerializer::SplitNameAndGuid(text, name, parsed))
				return false;

			guid = parsed.ToString();

			return true;
		};

		void WriteFields(std::string& text, const std::vector<Prefabs::Field>& fields, int depth)
		{
			for (const Prefabs::Field& field : fields)
				text += Indent(depth) + field.key + field_separator + field.value + '\n';
		};

		void WriteObject(std::string& text, const Prefabs::ObjectNode& node, int depth)
		{
			text += Indent(depth) + SceneSerializer::gameobject_keyword + node.name + '(' + node.guid + ")\n";

			WriteFields(text, node.fields, depth + 1);

			for (const Prefabs::ComponentNode& component : node.components)
			{
				text += Indent(depth + 1) + SceneSerializer::component_keyword + component.type + '(' + component.guid + ")\n";
				WriteFields(text, component.fields, depth + 2);
			};

			for (const Prefabs::ObjectNode& child : node.children)
				WriteObject(text, child, depth + 1);
		};

		#pragma endregion

		#pragma region Nodes

		bool Contains(const std::vector<std::string>& keys, const std::string& key)
		{
			return std::find(keys.begin(), keys.end(), key) != keys.end();
		};

		const Prefabs::Field* FieldNamed(const std::vector<Prefabs::Field>& fields, const std::string& key)
		{
			for (const Prefabs::Field& field : fields)
				if (field.key == key)
					return &field;

			return nullptr;
		};

		bool IsLink(const Prefabs::ComponentNode& component, bool top)
		{
			return top && component.type == Prefabs::PrefabType();
		};

		// All but the top object's links.
		std::vector<const Prefabs::ComponentNode*> ContentOf(const Prefabs::ObjectNode& node, bool top)
		{
			std::vector<const Prefabs::ComponentNode*> content{ };

			for (const Prefabs::ComponentNode& component : node.components)
				if (!IsLink(component, top))
					content.push_back(&component);

			return content;
		};

		// Every guid in the tree, by the order it is met in.
		void Number(const Prefabs::ObjectNode& node, bool top, std::map<std::string, size_t>& numbers)
		{
			numbers.emplace(node.guid, numbers.size());

			for (const Prefabs::ComponentNode* component : ContentOf(node, top))
				numbers.emplace(component->guid, numbers.size());

			for (const Prefabs::ObjectNode& child : node.children)
				Number(child, false, numbers);
		};

		#pragma endregion

		#pragma region Syncing

		// An instance keeps its own outside references, and a prefab has none.
		constexpr const char* outside = "out";

		struct Shaper
		{
			const std::vector<std::string>& transform_keys;
			Prefabs::Part part;
			std::map<std::string, size_t> numbers{ };

			// Only a GameObject's own fields are its transform.
			bool Skips(const Prefabs::Field& field, bool object) const
			{
				return object && part == Prefabs::Part::Shared && Contains(transform_keys, field.key);
			};

			std::string Value(const std::string& value) const
			{
				if (const auto number = numbers.find(value); number != numbers.end())
					return '#' + std::to_string(number->second);

				return IsGuid(value) ? outside : value;
			};

			void Fields(std::string& text, const std::vector<Prefabs::Field>& fields, bool object, int depth) const
			{
				for (const Prefabs::Field& field : fields)
					if (!Skips(field, object))
						text += Indent(depth) + field.key + '=' + Value(field.value) + '\n';
			};

			void Object(std::string& text, const Prefabs::ObjectNode& node, bool top, int depth) const
			{
				text += Indent(depth) + 'O';

				if (!top || part == Prefabs::Part::Whole)
					text += ' ' + node.name;

				text += '\n';

				Fields(text, node.fields, true, depth + 1);

				for (const Prefabs::ComponentNode* component : ContentOf(node, top))
				{
					text += Indent(depth + 1) + "C " + component->type + '\n';
					Fields(text, component->fields, false, depth + 2);
				};

				for (const Prefabs::ObjectNode& child : node.children)
					Object(text, child, false, depth + 1);
			};
		};

		struct Retargeter
		{
			const std::vector<std::string>& transform_keys;
			Prefabs::Part part;

			// Each of source's guids, to what it is in the result.
			std::map<std::string, std::string> guids{ };

			void Match(const Prefabs::ObjectNode& source, const Prefabs::ObjectNode* target, bool top)
			{
				guids[source.guid] = target ? target->guid : Guid::New().ToString();

				const std::vector<const Prefabs::ComponentNode*> components = ContentOf(source, top);
				const std::vector<const Prefabs::ComponentNode*> counterparts =
					target ? ContentOf(*target, top) : std::vector<const Prefabs::ComponentNode*>{ };

				for (size_t i = 0; i < components.size(); i++)
					guids[components[i]->guid] = i < counterparts.size() && counterparts[i]->type == components[i]->type
						? counterparts[i]->guid
						: Guid::New().ToString();

				for (size_t i = 0; i < source.children.size(); i++)
					Match(source.children[i], target && i < target->children.size() ? &target->children[i] : nullptr, false);
			};

			std::vector<Prefabs::Field> Fields(
				const std::vector<Prefabs::Field>& fields,
				const std::vector<Prefabs::Field>* counterparts,
				bool object) const
			{
				const bool shared = part == Prefabs::Part::Shared;
				std::vector<Prefabs::Field> result{ };

				for (const Prefabs::Field& field : fields)
				{
					const Prefabs::Field* kept = counterparts ? FieldNamed(*counterparts, field.key) : nullptr;
					std::string value = field.value;

					if (shared && object && Contains(transform_keys, field.key))
						value = kept ? kept->value : field.value;
					else if (const auto mapped = guids.find(field.value); mapped != guids.end())
						value = mapped->second;
					else if (shared && IsGuid(field.value))
						value = kept ? kept->value : null_reference;

					result.push_back({ field.key, value });
				};

				return result;
			};

			Prefabs::ObjectNode Build(const Prefabs::ObjectNode& source, const Prefabs::ObjectNode* target, bool top) const
			{
				Prefabs::ObjectNode result{ };

				result.name = top && target && part == Prefabs::Part::Shared ? target->name : source.name;
				result.guid = guids.at(source.guid);
				result.fields = Fields(source.fields, target ? &target->fields : nullptr, true);

				const std::vector<const Prefabs::ComponentNode*> components = ContentOf(source, top);
				const std::vector<const Prefabs::ComponentNode*> counterparts =
					target ? ContentOf(*target, top) : std::vector<const Prefabs::ComponentNode*>{ };

				for (size_t i = 0; i < components.size(); i++)
				{
					const Prefabs::ComponentNode* counterpart =
						i < counterparts.size() && counterparts[i]->type == components[i]->type ? counterparts[i] : nullptr;

					result.components.push_back({
						components[i]->type,
						guids.at(components[i]->guid),
						Fields(components[i]->fields, counterpart ? &counterpart->fields : nullptr, false) });
				};

				if (top && target)
					for (const Prefabs::ComponentNode& component : target->components)
						if (IsLink(component, true))
							result.components.push_back(component);

				for (size_t i = 0; i < source.children.size(); i++)
					result.children.push_back(
						Build(source.children[i], target && i < target->children.size() ? &target->children[i] : nullptr, false));

				return result;
			};
		};

		void Isolate(Prefabs::ObjectNode& node, const std::map<std::string, size_t>& inside)
		{
			const auto seal =
				[&](std::vector<Prefabs::Field>& fields)
				{
					for (Prefabs::Field& field : fields)
						if (IsGuid(field.value) && !inside.contains(field.value))
							field.value = null_reference;
				};

			seal(node.fields);

			for (Prefabs::ComponentNode& component : node.components)
				seal(component.fields);

			for (Prefabs::ObjectNode& child : node.children)
				Isolate(child, inside);
		};

		#pragma endregion

		#pragma region Merging

		const std::string* FindField(const std::vector<Prefabs::Field>& fields, const std::string& key)
		{
			for (const Prefabs::Field& field : fields)
				if (field.key == key)
					return &field.value;

			return nullptr;
		};

		std::vector<Prefabs::Field> MergeFields(
			const std::vector<Prefabs::Field>& base,
			const std::vector<Prefabs::Field>& ours,
			const std::vector<Prefabs::Field>& theirs)
		{
			std::vector<Prefabs::Field> merged{ };

			for (const Prefabs::Field& field : ours)
			{
				const std::string* was = FindField(base, field.key);
				const std::string* now = FindField(theirs, field.key);

				if (was == nullptr || *was != field.value)
					merged.push_back(field);
				else if (now)
					merged.push_back({ field.key, *now });
			};

			for (const Prefabs::Field& field : theirs)
				if (!FindField(ours, field.key) && !FindField(base, field.key))
					merged.push_back(field);

			return merged;
		};

		// Matched by place, as everything in a prefab is. What both added is
		// kept, and a list both changed otherwise is ours.
		template<typename Node, typename MergeOne>
		std::vector<Node> MergeList(const std::vector<Node>& base, const std::vector<Node>& ours, const std::vector<Node>& theirs, const MergeOne& merge)
		{
			const bool both_added = ours.size() > base.size() && theirs.size() > base.size();

			if (!both_added && base.size() != ours.size() && base.size() != theirs.size())
				return ours;

			const std::vector<Node>& lengths = both_added || base.size() == theirs.size() ? ours : theirs;
			std::vector<Node> merged{ };

			for (size_t i = 0; i < lengths.size(); i++)
				merged.push_back(i < base.size() ? merge(base[i], ours[i], theirs[i]) : lengths[i]);

			if (both_added)
				merged.insert(merged.end(), theirs.begin() + base.size(), theirs.end());

			return merged;
		};

		Prefabs::ComponentNode MergeComponent(const Prefabs::ComponentNode& base, const Prefabs::ComponentNode& ours, const Prefabs::ComponentNode& theirs)
		{
			if (ours.type != base.type || theirs.type != base.type)
				return ours.type != base.type ? ours : theirs;

			return { ours.type, ours.guid, MergeFields(base.fields, ours.fields, theirs.fields) };
		};

		Prefabs::ObjectNode MergeObject(const Prefabs::ObjectNode& base, const Prefabs::ObjectNode& ours, const Prefabs::ObjectNode& theirs)
		{
			return {
				ours.name == base.name ? theirs.name : ours.name,
				ours.guid,
				MergeFields(base.fields, ours.fields, theirs.fields),
				MergeList(base.components, ours.components, theirs.components, MergeComponent),
				MergeList(base.children, ours.children, theirs.children, MergeObject) };
		};

		#pragma endregion
	};

	#pragma region Links

	std::vector<std::string> Prefabs::TransformKeys(const GameObject& gameObject)
	{
		const void* transform[] =
		{
			&*gameObject.transform.position,
			&*gameObject.transform.rotation,
			&*gameObject.transform.scale,
		};

		const std::vector<SerializedField>& fields = gameObject.GetFields();
		std::vector<std::string> keys{ };

		for (size_t i = 0; i < fields.size(); i++)
			if (std::find(std::begin(transform), std::end(transform), fields[i].data) != std::end(transform))
				keys.push_back(std::to_string(i));

		return keys;
	};

	Prefab* Prefabs::LinkOf(const GameObject& gameObject)
	{
		Prefab* link = const_cast<GameObject&>(gameObject).GetComponent<Prefab>();

		return link && !link->path->empty() ? link : nullptr;
	};

	Prefab* Prefabs::NearestLink(const GameObject& gameObject)
	{
		for (const GameObject* at = &gameObject; at != nullptr; at = at->GetParent())
			if (Prefab* link = LinkOf(*at))
				return link;

		return nullptr;
	};

	const std::string& Prefabs::PrefabType()
	{
		static const std::string name = PrettyTypeName(typeid(Prefab).name());

		return name;
	};

	#pragma endregion

	#pragma region Text

	bool Prefabs::Parse(const std::string& text, ObjectNode& root, std::string* error)
	{
		// The object open at each depth, the top one first.
		std::vector<ObjectNode*> objects{ };
		ComponentNode* component = nullptr;
		int component_depth = -1;
		bool rooted = false;

		std::istringstream lines(text);
		std::string line;
		int number = 0;

		const auto fail =
			[&](const std::string& why)
			{
				if (error)
					*error = "line " + std::to_string(number) + ": " + why;

				return false;
			};

		while (std::getline(lines, line))
		{
			number++;

			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			size_t start = 0;
			const int depth = SceneSerializer::DepthOf(line, start);
			const std::string content = line.substr(start);

			if (content.empty() || content[0] == '#')
				continue;

			std::string name;
			std::string guid;

			if (StartsWith(content, SceneSerializer::gameobject_keyword))
			{
				if (!SplitNameAndGuid(content.substr(strlen(SceneSerializer::gameobject_keyword)), name, guid))
					return fail("a GameObject with no guid");

				component = nullptr;

				if (depth == 0)
				{
					if (rooted)
						return fail("a second GameObject at the top");

					rooted = true;
					root = ObjectNode{ name, guid };
					objects.assign(1, &root);
					continue;
				};

				if ((size_t)depth > objects.size())
					return fail("a GameObject deeper than anything above it");

				objects.resize((size_t)depth);
				objects.back()->children.push_back(ObjectNode{ name, guid });
				objects.push_back(&objects.back()->children.back());
				continue;
			};

			if (StartsWith(content, SceneSerializer::component_keyword))
			{
				if (depth == 0 || (size_t)depth > objects.size())
					return fail("a Component with no GameObject to be on");

				if (!SplitNameAndGuid(content.substr(strlen(SceneSerializer::component_keyword)), name, guid))
					return fail("a Component with no guid");

				objects.resize((size_t)depth);
				objects.back()->components.push_back(ComponentNode{ name, guid });
				component = &objects.back()->components.back();
				component_depth = depth;
				continue;
			};

			const size_t separator = content.find(field_separator);

			if (separator == std::string::npos || depth == 0)
				return fail("expected 'GameObject', 'Component' or '<index> = <value>'");

			const Field field{ content.substr(0, separator), content.substr(separator + strlen(field_separator)) };

			if (component && component_depth == depth - 1)
				component->fields.push_back(field);
			else if ((size_t)depth <= objects.size())
			{
				objects.resize((size_t)depth);
				objects.back()->fields.push_back(field);
			}
			else return fail("a field with nothing to be on");
		};

		return rooted || fail("there is no GameObject in it");
	};

	std::string Prefabs::Write(const ObjectNode& root)
	{
		std::string text{ };

		WriteObject(text, root, 0);

		return text;
	};

	std::string Prefabs::Splice(const std::string& scene, const std::string& guid, const std::string& subtree)
	{
		std::vector<std::string> lines{ };
		std::istringstream in(scene);

		for (std::string line; std::getline(in, line);)
			lines.push_back(line);

		size_t first = lines.size();
		int depth = 0;

		for (size_t i = 0; i < lines.size() && first == lines.size(); i++)
		{
			size_t start = 0;
			const int at = SceneSerializer::DepthOf(lines[i], start);
			const std::string content = lines[i].substr(start);

			std::string name;
			std::string found;

			if (StartsWith(content, SceneSerializer::gameobject_keyword) &&
				SplitNameAndGuid(content.substr(strlen(SceneSerializer::gameobject_keyword)), name, found) &&
				found == guid)
			{
				first = i;
				depth = at;
			};
		};

		if (first == lines.size())
			return scene;

		size_t end = first + 1;

		for (size_t start = 0; end < lines.size(); end++)
			if (lines[end].find_first_not_of(" \t\r") != std::string::npos && SceneSerializer::DepthOf(lines[end], start) <= depth)
				break;

		std::string text{ };

		for (size_t i = 0; i < first; i++)
			text += lines[i] + '\n';

		std::istringstream replacement(subtree);

		for (std::string line; std::getline(replacement, line);)
			text += Indent(depth) + line + '\n';

		for (size_t i = end; i < lines.size(); i++)
			text += lines[i] + '\n';

		return text;
	};

	#pragma endregion

	#pragma region Syncing

	std::string Prefabs::Shape(const ObjectNode& root, const std::vector<std::string>& transform_keys, Part part)
	{
		Shaper shaper{ transform_keys, part };
		Number(root, true, shaper.numbers);

		std::string text{ };
		shaper.Object(text, root, true, 0);

		return text;
	};

	Prefabs::ObjectNode Prefabs::Retarget(
		const ObjectNode& source,
		const ObjectNode& target,
		const std::vector<std::string>& transform_keys,
		Part part)
	{
		Retargeter retargeter{ transform_keys, part };
		retargeter.Match(source, &target, true);

		return retargeter.Build(source, &target, true);
	};

	Prefabs::ObjectNode Prefabs::Isolated(const ObjectNode& root)
	{
		std::map<std::string, size_t> inside{ };
		Number(root, true, inside);

		ObjectNode isolated = root;
		Isolate(isolated, inside);

		return isolated;
	};

	Prefabs::ObjectNode Prefabs::Merge(
		const ObjectNode& base,
		const ObjectNode& ours,
		const ObjectNode& theirs,
		const std::vector<std::string>& transform_keys)
	{
		// On ours' guids, so references compare equal where they agree.
		return MergeObject(
			Retarget(base, ours, transform_keys, Part::Whole),
			ours,
			Retarget(theirs, ours, transform_keys, Part::Whole));
	};

	Prefabs::Outcome Prefabs::Sync(
		const ObjectNode& prefab,
		const ObjectNode* panel,
		const std::string& panel_shape,
		const std::vector<Instance>& instances,
		const std::vector<std::string>& transform_keys,
		bool save)
	{
		Outcome outcome{ };
		outcome.instances.resize(instances.size());

		const std::string shared = Shape(prefab, transform_keys, Part::Shared);
		const bool edited = panel && Shape(*panel, transform_keys, Part::Whole) != panel_shape;

		if (edited && save)
			outcome.prefab = Retarget(*panel, prefab, transform_keys, Part::Whole);
		else
			for (const Instance& instance : instances)
				if (instance.known && Shape(instance.tree, transform_keys, Part::Shared) != shared)
				{
					outcome.prefab = Retarget(instance.tree, prefab, transform_keys, Part::Shared);

					if (edited)
						outcome.panel = Merge(prefab, *panel, *outcome.prefab, transform_keys);
					else outcome.reshow = panel != nullptr;

					break;
				};

		const ObjectNode& agreed = outcome.prefab ? *outcome.prefab : prefab;
		const std::string agreed_shape = outcome.prefab ? Shape(agreed, transform_keys, Part::Shared) : shared;

		for (size_t i = 0; i < instances.size(); i++)
			if (Shape(instances[i].tree, transform_keys, Part::Shared) != agreed_shape)
				outcome.instances[i] = Retarget(agreed, instances[i].tree, transform_keys, Part::Shared);

		return outcome;
	};

	#pragma endregion
};

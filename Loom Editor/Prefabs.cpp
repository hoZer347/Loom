#include "Prefabs.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Guid.h"
#include "Prefab.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>


namespace Loom
{
	namespace
	{
		constexpr const char* object_keyword = "GameObject ";
		constexpr const char* component_keyword = "Component ";
		constexpr const char* field_separator = " = ";

		// What a reference out of the tree compares as: an instance keeps its
		// own, and a prefab has nothing outside it to point at.
		constexpr const char* outside = "out";

		// Four spaces count as a tab, as they do for SceneSerializer.
		constexpr int spaces_per_tab = 4;

		std::string Indent(int depth)
		{
			return std::string((size_t)depth, '\t');
		};

		int DepthOf(const std::string& line, size_t& content_start)
		{
			int depth = 0;
			int spaces = 0;
			size_t at = 0;

			for (; at < line.size(); at++)
			{
				if (line[at] == '\t')
				{
					depth++;
					spaces = 0;
				}
				else if (line[at] == ' ')
				{
					if (++spaces == spaces_per_tab)
					{
						depth++;
						spaces = 0;
					};
				}
				else break;
			};

			content_start = at;

			return depth;
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

		// "Player(0f8e...)": the last brackets hold the guid, so a name may have
		// brackets of its own.
		bool SplitNameAndGuid(const std::string& text, std::string& name, std::string& guid)
		{
			const size_t open = text.find_last_of('(');
			const size_t close = text.find_last_of(')');

			if (open == std::string::npos || close == std::string::npos || close < open)
				return false;

			name = text.substr(0, open);
			guid = text.substr(open + 1, close - open - 1);

			return IsGuid(guid);
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

		// The components that are content, which is all of them but the top
		// object's links.
		std::vector<const Prefabs::ComponentNode*> ContentOf(const Prefabs::ObjectNode& node, bool top)
		{
			std::vector<const Prefabs::ComponentNode*> content{ };

			for (const Prefabs::ComponentNode& component : node.components)
				if (!IsLink(component, top))
					content.push_back(&component);

			return content;
		};

		void WriteFields(std::string& text, const std::vector<Prefabs::Field>& fields, int depth)
		{
			for (const Prefabs::Field& field : fields)
				text += Indent(depth) + field.key + field_separator + field.value + '\n';
		};

		void WriteObject(std::string& text, const Prefabs::ObjectNode& node, int depth)
		{
			text += Indent(depth) + object_keyword + node.name + '(' + node.guid + ")\n";

			WriteFields(text, node.fields, depth + 1);

			for (const Prefabs::ComponentNode& component : node.components)
			{
				text += Indent(depth + 1) + component_keyword + component.type + '(' + component.guid + ")\n";
				WriteFields(text, component.fields, depth + 2);
			};

			for (const Prefabs::ObjectNode& child : node.children)
				WriteObject(text, child, depth + 1);
		};

		struct Shaper
		{
			const std::vector<std::string>& transform_keys;
			Prefabs::Part part;

			// Every guid in the tree, by the order it is met in.
			std::map<std::string, size_t> numbers{ };

			void Number(const Prefabs::ObjectNode& node, bool top)
			{
				numbers.emplace(node.guid, numbers.size());

				for (const Prefabs::ComponentNode* component : ContentOf(node, top))
					numbers.emplace(component->guid, numbers.size());

				for (const Prefabs::ObjectNode& child : node.children)
					Number(child, false);
			};

			// Only a GameObject's own fields are its transform.
			bool Skips(const Prefabs::Field& field, bool object) const
			{
				return object &&
					part == Prefabs::Part::Shared &&
					std::find(transform_keys.begin(), transform_keys.end(), field.key) != transform_keys.end();
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

			bool IsTransform(const std::string& key) const
			{
				return std::find(transform_keys.begin(), transform_keys.end(), key) != transform_keys.end();
			};

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
				bool transforms) const
			{
				const bool shared = part == Prefabs::Part::Shared;
				std::vector<Prefabs::Field> result{ };

				for (const Prefabs::Field& field : fields)
				{
					const Prefabs::Field* kept = counterparts ? FieldNamed(*counterparts, field.key) : nullptr;
					std::string value = field.value;

					if (shared && transforms && IsTransform(field.key))
						value = kept ? kept->value : field.value;
					else if (const auto mapped = guids.find(field.value); mapped != guids.end())
						value = mapped->second;
					else if (shared && IsGuid(field.value))
						value = kept ? kept->value : "null";

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
	};

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

	const std::string& Prefabs::PrefabType()
	{
		static const std::string name = PrettyTypeName(typeid(Prefab).name());

		return name;
	};

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
			const int depth = DepthOf(line, start);
			const std::string content = line.substr(start);

			if (content.empty() || content[0] == '#')
				continue;

			std::string name;
			std::string guid;

			if (StartsWith(content, object_keyword))
			{
				if (!SplitNameAndGuid(content.substr(strlen(object_keyword)), name, guid))
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

			if (StartsWith(content, component_keyword))
			{
				if (depth == 0 || (size_t)depth > objects.size())
					return fail("a Component with no GameObject to be on");

				if (!SplitNameAndGuid(content.substr(strlen(component_keyword)), name, guid))
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

	std::string Prefabs::Shape(const ObjectNode& root, const std::vector<std::string>& transform_keys, Part part)
	{
		Shaper shaper{ transform_keys, part };
		shaper.Number(root, true);

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
			const int at = DepthOf(lines[i], start);
			const std::string content = lines[i].substr(start);

			std::string name;
			std::string found;

			if (StartsWith(content, object_keyword) &&
				SplitNameAndGuid(content.substr(strlen(object_keyword)), name, found) &&
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
			if (lines[end].find_first_not_of(" \t\r") != std::string::npos && DepthOf(lines[end], start) <= depth)
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
};

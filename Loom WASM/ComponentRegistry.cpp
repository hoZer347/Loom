#include "ComponentRegistry.h"

#include "Material.h"
#include "Mesh.h"

#include <cctype>
#include <cstring>
#include <iterator>


namespace Loom
{
	std::map<std::string, ComponentRegistry::Factory>& ComponentRegistry::Factories()
	{
		static std::map<std::string, Factory> factories{ };
		return factories;
	};

	std::map<std::string, std::string>& ComponentRegistry::DisplayNames()
	{
		static std::map<std::string, std::string> names{ };
		return names;
	};

	void ComponentRegistry::Register(
		const std::string& name,
		const char* type_name,
		const Factory& factory)
	{
		Factories()[name] = factory;

		if (type_name)
			DisplayNames()[type_name] = name;
	};

	ComponentBase* ComponentRegistry::Create(const std::string& name, GameObject& gameObject)
	{
		const auto found = Factories().find(name);

		return found == Factories().end()
			? nullptr
			: found->second(gameObject);
	};

	void ComponentRegistry::Unregister(const std::string& name)
	{
		Factories().erase(name);

		for (auto it = DisplayNames().begin(); it != DisplayNames().end(); )
			it = it->second == name
				? DisplayNames().erase(it)
				: std::next(it);
	};

	const std::map<std::string, ComponentRegistry::Factory>& ComponentRegistry::All()
	{
		return Factories();
	};

	std::string ComponentRegistry::NameOf(const ComponentBase& component)
	{
		const char* type_name = component.GetTypeName();

		if (type_name == nullptr)
			return "<unknown>";

		const auto found = DisplayNames().find(type_name);

		return found == DisplayNames().end()
			? PrettyTypeName(type_name)
			: found->second;
	};

	void ComponentRegistry::RegisterBuiltins()
	{
		// GL_TRIANGLES, spelled out so this file does not have to pull in GL
		Register<Mesh>("Mesh", (uint32_t)0x0004);
		Register<Material>("Material");
	};

	std::string PrettyTypeName(const char* type_name)
	{
		if (type_name == nullptr)
			return "<unknown>";

		std::string name = type_name;

		// MSVC spells it out: "struct Loom::Mesh".
		bool spelled_out = false;

		for (const char* prefix : { "struct ", "class ", "enum " })
			if (name.rfind(prefix, 0) == 0)
			{
				name.erase(0, strlen(prefix));
				spelled_out = true;
			};

		const size_t namespace_end = name.rfind("::");

		if (spelled_out || namespace_end != std::string::npos)
		{
			if (namespace_end != std::string::npos)
				name.erase(0, namespace_end + 2);

			return name;
		};

		// Clang and gcc give the mangled form instead: "N4Loom4MeshE", or
		// "7Example" for a type outside any namespace. Each piece is its own
		// length followed by its characters, and the last piece is the type.
		size_t at = name.rfind('N', 0) == 0 ? 1 : 0;

		std::string last;

		while (at < name.size() && isdigit((unsigned char)name[at]))
		{
			size_t length = 0;

			while (at < name.size() && isdigit((unsigned char)name[at]))
				length = length * 10 + (size_t)(name[at++] - '0');

			if (length == 0 || at + length > name.size())
				break;

			last = name.substr(at, length);

			at += length;
		};

		return last.empty()
			? name
			: last;
	};
};

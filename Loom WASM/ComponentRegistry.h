#pragma once

#include "Loom API.h"

#include "GameObject.h"

#include <functional>
#include <map>
#include <string>
#include <typeinfo>


namespace Loom
{
	/**
	* Loom::ComponentRegistry
	* - The component types that can be named
	* - A scene file says "Component Mesh(...)", and something has to turn that
	*   back into an actual Mesh on an actual GameObject; the same list is what
	*   the editor offers under "Add Component"
	*/
	struct LOOM_API ComponentRegistry final
	{
		typedef std::function<ComponentBase* (GameObject&)> Factory;

		static void Register(
			const std::string& name,
			const char* type_name,
			const Factory& factory);

		// Registers T under a display name, constructing it with the given
		// arguments every time one is created. The arguments are copied into the
		// factory, so they have to outlive nothing.
		template <typename T, typename... Args>
		static void Register(const std::string& name, Args... args)
		{
			Register(
				name,
				typeid(T).name(),
				[args...](GameObject& gameObject) -> ComponentBase*
				{
					return gameObject.Attach<T>(args...);
				});
		};

		// Attaches one by registered name. Null when nothing is registered under
		// that name, which is how loading a scene that uses a component this
		// build does not have degrades rather than fails.
		static ComponentBase* Create(const std::string& name, GameObject& gameObject);

		// Takes a type back out again: a script library is unloaded before it is
		// rebuilt, and a factory pointing into code that is no longer mapped is
		// a crash waiting for someone to open the Add Component menu.
		static void Unregister(const std::string& name);

		static const std::map<std::string, Factory>& All();

		// What to call this component in a file or a header: its registered name
		// when it has one, otherwise its type name tidied up.
		static std::string NameOf(const ComponentBase& component);

		// The component types that ship with the engine. Called by Engine.
		static void RegisterBuiltins();

	private:
		static std::map<std::string, Factory>& Factories();
		static std::map<std::string, std::string>& DisplayNames();
	};

	// typeid().name() gives "struct Loom::Mesh" on MSVC, which is not what you
	// want filling an inspector header; this trims it down to "Mesh".
	std::string PrettyTypeName(const char* type_name);
};

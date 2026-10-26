#pragma once

#include "Loom API.h"

#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <typeinfo>


namespace Loom
{
	struct ComponentBase;
	struct GameObject;

	// typeid().name() gives "struct Loom::Mesh" on MSVC, which is not what you
	// want filling an inspector header; this trims it down to "Mesh".
	LOOM_API std::string PrettyTypeName(const char* type_name);

	/**
	* Loom::ComponentRegistry
	* - The component types that can be named
	* - A scene file says "Component Mesh(...)", and something has to turn that
	*   back into an actual Mesh on an actual GameObject; the same list is what
	*   the editor offers under "Add Component"
	* - Every default-constructible Component<T> puts itself in here, under its
	*   own type name, as the statics of each module that includes it start up
	* - A type that names a category after itself is filed under it in the Add
	*   Component menu, with '/' between nested categories:
	*
	*	struct Patrol : Component<Patrol, "Gameplay/AI"> { };
	*/
	struct LOOM_API ComponentRegistry final
	{
		typedef std::function<ComponentBase* (GameObject&)> Factory;

		#pragma region Registration

		static void Register(
			const std::string& name,
			const char* type_name,
			const Factory& factory,
			const std::string& category = "");

		// Registers T under a display name, constructing it with the given
		// arguments every time one is created. The arguments are copied into the
		// factory, so they have to outlive nothing.
		template <typename T, typename... Args>
		static void Register(const std::string& name, Args... args)
		{
			// Generic so the call is only resolved once T is: GameObject includes
			// this header by way of Component, so it is not complete yet here.
			Register(
				name,
				typeid(T).name(),
				[args...](auto& gameObject) -> ComponentBase*
				{
					return gameObject.template Attach<T>(args...);
				},
				T::ComponentCategory);
		};

		// What Component<T> runs for every type derived from it. One that cannot
		// be built from nothing has no place in a menu, and is left to register
		// itself with the arguments it needs.
		template <typename T>
		static bool RegisterType()
		{
			if constexpr (std::is_default_constructible_v<T>)
			{
				const std::string name = PrettyTypeName(typeid(T).name());

				if (IsFree(name, typeid(T).name()))
					Register<T>(name);
			};

			return true;
		};

		// Takes a type back out again: a script library is unloaded before it is
		// rebuilt, and a factory pointing into code that is no longer mapped is
		// a crash waiting for someone to open the Add Component menu.
		static void Unregister(const std::string& name);

		#pragma endregion

		#pragma region Lookup

		// Attaches one by registered name. Null when nothing is registered under
		// that name, where a scene being read puts a MissingComponent instead.
		static ComponentBase* Create(const std::string& name, GameObject& gameObject);

		static const std::map<std::string, Factory>& All();

		// Where a registered name files itself in the Add Component menu. Empty
		// for a type that declares no category.
		static std::string CategoryOf(const std::string& name);

		// What to call this component in a file or a header: its registered name
		// when it has one, otherwise its type name tidied up. A MissingComponent
		// is called what it stands in for.
		static std::string NameOf(const ComponentBase& component);

		#pragma endregion

	private:
		#pragma region Registration

		// Whether a type registering itself may take the name. Not when it
		// already has it, which is a second module including the same type, and
		// not when another type does: the names drop namespaces, and whichever
		// came first keeps it.
		static bool IsFree(const std::string& name, const char* type_name);

		#pragma endregion

		#pragma region Storage

		static std::map<std::string, Factory>& Factories();
		static std::map<std::string, std::string>& DisplayNames();
		static std::map<std::string, std::string>& Categories();

		#pragma endregion
	};
};

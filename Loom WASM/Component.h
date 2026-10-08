#pragma once

#include "Loom API.h"

#include "ComponentRegistry.h"
#include "imgui.h"
#include "LoomObject.h"

#include <typeinfo>
#include <type_traits>
#include <iostream>


namespace Loom
{
	struct GameObject;

	/// Summary:
	/// * ComponentBase:
	/// * - Untemplated base class for all components
	/// * - Defines the interface for all components
	/// * - Tracks the parent GameObject and typeinfo as well
	//
	struct LOOM_API ComponentBase :
		public LoomObject
	{
		constexpr virtual void OnAttach()	{ };
		constexpr virtual void OnDetach()	{ };
		constexpr virtual void OnGui()		{ };
		constexpr virtual void OnUpdate()	{ };
		constexpr virtual void OnRender()	{ };
		constexpr virtual void OnPhysics()	{ };

		virtual ~ComponentBase() { };

		const std::string& GetClassName() const { return m_name; };

		// The typeid name of the concrete component, filled in by Component<T>.
		// This is what identifies a component in the editor's inspector.
		const char* GetTypeName() const { return m_type_name; };
		GameObject const* GetGameObject() const { return m_gameObject; };

	protected:
		friend struct GameObject;
		GameObject* m_gameObject = nullptr;

		virtual void Gui() { };

		// Drawn under OnGui in the same fold-out, for a component type the
		// engine builds on, so its own rows leave OnGui to whoever writes one.
		virtual void ExtraGui() { };

		// The fold-out the built-in GUI wraps a component in. Lives here
		// rather than in Component<T> so a script module instantiating a
		// component of its own does not have to link a UI to do it.
		void DrawDefaultGui();

		const char* m_type_name;
	};


	template <typename T>
		struct Component :
		public ComponentBase
	{
		virtual ~Component() { };

	protected:
		Component()
		{
			m_type_name = typeid(T).name();
		};

		void Gui() override
		{
			DrawDefaultGui();
		};

	private:
		static inline const bool s_registered = ComponentRegistry::RegisterType<T>();

		// A static member is only defined once something uses it, and nothing in
		// T would. Naming it in a typedef does: the typedef is instantiated with
		// this class, which is the moment T derives from it.
		typedef std::integral_constant<const bool*, &s_registered> Registration;
	};
};

// The factory Component<T> registers attaches a T to a GameObject, so a file
// that defines a component needs the whole of it by the time it ends.
#include "GameObject.h"

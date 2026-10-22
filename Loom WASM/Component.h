#pragma once

#include "Loom API.h"

#include "ComponentRegistry.h"
#include "imgui.h"
#include "LoomObject.h"
#include "Macro Helpers.h"

#include <typeinfo>
#include <type_traits>
#include <iostream>


namespace Loom
{
	struct Collider;
	struct Collision;
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

		// Called on every component of a GameObject whose collider starts,
		// keeps on or stops touching another, at most once a physics step for
		// each pair, as Unity's are. Collisions need a Rigidbody on at least
		// one side; triggers report overlaps in place of collisions.
		constexpr virtual void OnCollisionEnter(const Collision&)	{ };
		constexpr virtual void OnCollisionStay(const Collision&)	{ };
		constexpr virtual void OnCollisionExit(const Collision&)	{ };
		constexpr virtual void OnTriggerEnter(Collider&)			{ };
		constexpr virtual void OnTriggerStay(Collider&)				{ };
		constexpr virtual void OnTriggerExit(Collider&)				{ };

		virtual ~ComponentBase() { };

		const std::string& GetClassName() const { return m_name; };

		// The typeid name of the concrete component, filled in by Component<T>.
		// This is what identifies a component in the editor's inspector.
		const char* GetTypeName() const { return m_type_name; };
		GameObject const* GetGameObject() const { return m_gameObject; };
		GameObject* GetGameObject() { return m_gameObject; };

		// Whether it overrides any of the collision or trigger callbacks.
		bool HearsContacts() const { return m_hearsContacts; };

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

		bool m_hearsContacts = false;
	};


	// category files T in the Add Component menu, with '/' between nested
	// categories. Base is for a family of components the engine handles
	// together, such as every Collider: an abstract ComponentBase the family
	// shares, which T derives from through this rather than alongside it.
	template <typename T, Str category = "", typename Base = ComponentBase>
		struct Component :
		public Base
	{
		static_assert(std::is_base_of_v<ComponentBase, Base>, "A component's base must be a ComponentBase");

		// What GameObject checks a type it attaches against: T itself, not a
		// type derived from it.
		typedef T ComponentType;

		static constexpr const char* ComponentCategory = category.value;

		virtual ~Component() { };

	protected:
		Component()
		{
			this->m_type_name = typeid(T).name();
		};

		void Gui() override
		{
			this->DrawDefaultGui();
		};

	private:
		static inline const bool s_registered = ComponentRegistry::RegisterType<T>();

		// A static member is only defined once something uses it, and nothing in
		// T would. Naming it in a typedef does: the typedef is instantiated with
		// this class, which is the moment T derives from it.
		typedef std::integral_constant<const bool*, &s_registered> Registration;
	};

	template <typename T>
	concept AComponent = std::is_same_v<typename T::ComponentType, T>;

	// The abstract base a family of components shares, such as Collider,
	// which can be looked for but not attached.
	template <typename T>
	concept AComponentFamily = std::is_abstract_v<T> && std::is_base_of_v<ComponentBase, T>;
};

// The factory Component<T> registers attaches a T to a GameObject, so a file
// that defines a component needs the whole of it by the time it ends.
#include "GameObject.h"

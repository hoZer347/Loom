#pragma once

#include "Loom API.h"

#include "imgui.h"
#include "LoomObject.h"

#include <typeinfo>
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
		GameObject* m_gameObject;

		virtual void Gui() { };

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
	};
};

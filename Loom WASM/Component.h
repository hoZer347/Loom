#pragma once

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
	struct ComponentBase :
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
		GameObject const* GetGameObject() const { return m_gameObject; };

	protected:
		friend struct GameObject;
		GameObject* m_gameObject;

		virtual void Gui() { };
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
			ImGui::PushID(this);

			if (ImGui::TreeNode(
				(void*)this,
				typeid(T).name()))
			{
				OnGui();
				ImGui::TreePop();
			};

			ImGui::PopID();
		};
	};
};

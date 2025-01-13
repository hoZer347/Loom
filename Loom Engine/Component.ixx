export module Component;

import LoomObject;

import "imgui.h";
import "typeinfo";

import <iostream>;
import <boost/type_index.hpp>;


namespace Loom
{
	struct GameObject;


	export struct ComponentBase :
		public LoomObject
	{
		virtual void OnAttach()		 { };
		virtual void OnDetach()		 { };
		virtual void OnGui()		 { };
		virtual void OnRender()		 { };
		virtual void OnPhysics()	 { };
		virtual void OnPopOutGui()	 { };
		virtual void OnRefresh()     { };

		virtual ~ComponentBase()	 { };

		const std::string& GetClassName() const { return m_name; };
		GameObject const* GetGameObject() const { return m_gameObject; };

	protected:
		friend struct GameObject;
		GameObject* m_gameObject;

		virtual void Gui() { };
		size_t m_type_name;
	};

	export template <typename T>
		struct Component :
		public ComponentBase
	{
		virtual ~Component() { };

	protected:
		Component() { };

	private:
		void Gui() override
		{
			ImGui::PushID(this);
			
			if (ImGui::TreeNode(
				(void*)this,
				boost::typeindex::type_id<T>().pretty_name().c_str()))
			{
				OnGui();
				ImGui::TreePop();
			};

			ImGui::PopID();
		};
	};
};

export module Component;

import LoomObject;

import "imgui.h";
import "typeinfo";
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
		virtual void OnUpdate()		 { };
		virtual void OnRender()		 { };
		virtual void OnPhysics()	 { };
		virtual void OnPopOutGui()	 { };
		virtual void OnRefresh()     { };

		virtual ~ComponentBase()	 { };

		GameObject const* GetGameObject() const { return m_game_object; };
		const std::string& GetName() const { return m_name; };

	protected:
		friend struct GameObject;
		virtual void Gui() { };
		size_t m_type_id;
		GameObject* m_game_object;
	};

	export template <typename T>
		struct Component :
		public ComponentBase
	{
		virtual ~Component() { };

	private:
		void Gui() override
		{
			ImGui::PushID(this);
			
			if (ImGui::TreeNode((void*)this, boost::typeindex::type_id<T>().pretty_name().c_str()))
			{
				OnGui();
				ImGui::TreePop();
			};

			ImGui::PopID();
		};
	};
};

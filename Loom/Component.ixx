export module Component;

import "imgui.h";
import "typeinfo";
import <boost/type_index.hpp>;


namespace Loom
{
	struct GameObject;

	export struct ComponentBase
	{
		virtual void OnAttach()		{ };
		virtual void OnDetach()		{ };
		virtual void OnGui()		{ };
		virtual void OnUpdate()		{ };
		virtual void OnRender()		{ };
		virtual void OnPopOutGui()	{ };

		virtual ~ComponentBase()	{ };

	protected:
		friend struct GameObject;
		virtual void Gui() { };
		size_t type_id;
	};

	template <typename T>
	const char* disp()
	{
		return typeid(T).name();
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

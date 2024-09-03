export module GameObject;

import Component;
import Engine;

import <string>;
import <vector>;
import <atomic>;
import <iostream>;


namespace Loom
{
	export struct GameObject final
	{
		template <typename T>
		T* Attach(auto&&... args)
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");
			
			T* component = new T(args...);
			((ComponentBase*)component)->type_id = typeid(T).hash_code();

			engine->QueueTask(
				[this, component]()
				{
					m_components.emplace_back(component);

					if constexpr (&T::OnUpdate != &ComponentBase::OnUpdate)
					{
						std::cout << "Updateable added" << std::endl;
						m_updateables.emplace_back(component);
					};

					if constexpr (&T::OnRender != &ComponentBase::OnRender)
					{
						std::cout << "Renderable added" << std::endl;
						m_renderables.emplace_back(component);
					};

					if constexpr (&T::OnAttach != &ComponentBase::OnAttach)
						component->OnAttach();
				});

			return component;
		};

		template <typename T>
		_NODISCARD T* GetComponent()
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			for (auto& component : m_components)
				if (component->type_id = typeid(T).hash_code())
					return (T*)component;

			return nullptr;
		};

		template <typename T>
		bool RmvComponent()
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			for (auto& component : m_components)
				if (component->type_id = typeid(T).hash_code())
				{
					engine->QueueTask(
						[component]()
						{
							m_components.erase(
								std::remove(
									m_components.begin(),
									m_components.end(),
									component));

							if constexpr (&T::OnUpdate != &ComponentBase::OnUpdate)
								m_updateables.erase(
									std::remove(
										m_updateables.begin(),
										m_updateables.end(),
										component));

							if constexpr (&T::OnRender != &ComponentBase::OnRender)
								m_renderables.erase(
									std::remove(
										m_renderables.begin(),
										m_renderables.end(),
										component));

							if constexpr (&T::OnDetach != &ComponentBase::OnDetach)
								component->OnDetach();

							delete component;
						});
					
					return true;
				};

			return false;
		};

		void RemoveChild(GameObject* gameObject);

		void SetThreadID(int thread_id);

		void Destroy();

		GameObject* AddChild(const char* name);

	protected:
		friend struct Scene;
		friend struct Engine;

		GameObject(Engine* engine, GameObject* parent, const char* name, const int& thread_id);

		~GameObject();

		const size_t m_id = id_counter++;
		static inline std::atomic<size_t> num_objects = 0;
		static inline std::atomic<size_t> id_counter = 0;

		bool m_inherit_thread_id;
		int m_threadID = 0;

		std::vector<GameObject*>	m_children{ };
		std::vector<ComponentBase*> m_components{ };
		std::vector<ComponentBase*> m_updateables{ };
		std::vector<ComponentBase*> m_renderables{ };
		std::vector<ComponentBase*> m_physicsables{ };

	private:
		GameObject* parent;
		Engine* engine;

		char newName[128];
		const char* name;
		
		void Update(const int& thread);
		void Render();
		void Physics();
		void Gui();
		
		virtual void Gui() { };
		virtual void _Gui();
	};
};

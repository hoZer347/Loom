#pragma once

#include "LoomObject.h"

namespace Loom
{
	struct GameObject final :
		public LoomObject
	{
		template <typename T>
		T* Attach(auto&&... args)
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			T* component = new T(args...);
			((ComponentBase*)component)->m_type_id = typeid(T).hash_code();

			Engine::QueueTask(
				[this, component]()
				{
					((ComponentBase*)component)->m_gameObject = (decltype(((ComponentBase*)component)->m_gameObject))(void*)this;

					m_components.emplace_back(component);

					if constexpr (&T::OnUpdate != &ComponentBase::OnUpdate)
						m_updateables.emplace_back(component);

					if constexpr (&T::OnRender != &ComponentBase::OnRender)
						m_renderables.emplace_back(component);

					if constexpr (&T::OnAttach != &ComponentBase::OnAttach)
						component->OnAttach();
				});

			return component;
		};

		template <typename T>
		[[nodiscard]] T* GetComponent()
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			for (auto& component : m_components)
				if (component->m_type_id = typeid(T).hash_code())
					return (T*)component;

			return nullptr;
		};

		template <typename T>
		bool Detach()
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			for (auto& component : m_components)
				if (component->m_type_id = typeid(T).hash_code())
				{
					Engine::QueueTask(
						[this, component]()
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

		GameObject* AddChild(const std::string& name = "New GameObject");

		template <typename T>
		static inline void RegisterComponent()
		{
			std::cout << "Registered: " << boost::typeindex::type_id<T>().pretty_name() << std::endl;

			reg_component_attachers[boost::typeindex::type_id<T>().pretty_name()] =
				[](void* gameObject)
				{
					((GameObject*)gameObject)->Attach<T>();
				};

			reg_component_detachers[boost::typeindex::type_id<T>().pretty_name()] =
				[](void* gameObject)
				{
					((GameObject*)gameObject)->Detach<T>();
				};
		};

	protected:
		friend struct Scene;
		friend struct Engine;
		friend struct Serialize;

		GameObject(GameObject* parent, const std::string& name, const int& thread_id);

		virtual ~GameObject();

		static inline std::atomic<size_t> num_objects = 0;
		static inline std::atomic<size_t> id_counter = 0;

		bool m_inherit_thread_id;
		int m_threadID = 0;

		std::vector<GameObject*>	m_children{ };
		std::vector<ComponentBase*> m_components{ };
		std::vector<ComponentBase*> m_updateables{ };
		std::vector<ComponentBase*> m_renderables{ };
		std::vector<ComponentBase*> m_physicsables{ };

		SERIALIZE(
			&m_inherit_thread_id,
			&m_children,
			&m_components,
			&m_updateables,
			&m_renderables,
			&m_physicsables);

	private:
		static inline std::unordered_map<
			std::string,								// Name
			void(*)(void*)> reg_component_attachers{ };	// Attach function for attaching / detaching components

		static inline std::unordered_map<
			std::string,								// Name
			void(*)(void*)> reg_component_detachers{ };	// Detach function for attaching / detaching components

		GameObject* parent;

		char newName[128];

		void Update(const int& thread);
		void Render();
		void Physics();
		void Gui();
	};
};

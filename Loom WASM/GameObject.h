#pragma once

#include "Loom API.h"

#include "Engine.h"
#include "Component.h"
#include "LoomObject.h"
#include "Transform.h"

#include "glm/glm.hpp"

#include <cstring>
#include <string>
#include <atomic>
#include <vector>

#include <typeinfo>
#include <type_traits>


namespace Loom
{
	// Trait to detect if a method exists and matches a given signature
	template <typename T, typename Ret, typename... Args>
	struct has_method {
		template <typename U>
		static auto test(U*) -> decltype(std::is_same_v<decltype(&U::OnAttach), Ret(U::*)(Args...)>, std::true_type{});

		template <typename>
		static std::false_type test(...);

		static constexpr bool value = decltype(test<T>(nullptr))::value;
	};

	// Detect override of OnAttach
	template <typename T>
	struct overrides_on_attach
		: std::integral_constant<bool,
		has_method<T, void>::value &&
		!std::is_same_v<decltype(&T::OnAttach), decltype(&ComponentBase::OnAttach)>> { };

	// Detect override of OnRender
	template <typename T>
	struct overrides_on_render
		: std::integral_constant<bool,
		has_method<T, void>::value &&
		!std::is_same_v<decltype(&T::OnRender), decltype(&ComponentBase::OnRender)>> { };

	// Detect override of OnUpdate
	template <typename T>
	struct overrides_on_update
		: std::integral_constant<bool,
		has_method<T, void>::value &&
		!std::is_same_v<decltype(&T::OnUpdate), decltype(&ComponentBase::OnUpdate)>> { };

	// TODO: Turn the above templates into a macro

	struct LOOM_API GameObject final :
		public LoomObject
	{
		template <typename T>
		T* Attach(auto&&... args)
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			T* component = new T(args...);

			// The type name is Component<T>'s doing; what Attach adds is the owner.
			((ComponentBase*)component)->m_gameObject = this;

			// On the object immediately, so GetComponent finds it. Two components
			// attached in the same frame - by a scene load, or by one component's
			// OnAttach reaching for another - are siblings as soon as they exist,
			// not a frame later.
			m_components.emplace_back(component);

			// What has to wait is the work that runs them: the update and render
			// lists are walked mid-frame, and OnAttach expects the rest of the
			// object to be there.
			Engine::QueueTask(
				[this, component]()
				{
					if constexpr (overrides_on_update<T>::value)
						m_updateables.emplace_back(component);

					if constexpr (overrides_on_render<T>::value)
						m_renderables.emplace_back(component);

					if constexpr (overrides_on_attach<T>::value)
						component->OnAttach();
				});

			return component;
		};

		template <typename T>
		[[nodiscard]] T* GetComponent()
		{
			static_assert(std::is_base_of_v<Component<T>, T>, "Must be a component");

			// Compared by value, not by pointer: a script library carries its own
			// copy of the type name, so the addresses differ across the module
			// boundary even though the type is the same one.
			for (auto& component : m_components)
				if (strcmp(component->m_type_name, typeid(T).name()) == 0)
					return (T*)component;

			return nullptr;
		};

		// Depth first, the first T on this object or anywhere under it.
		template <typename T>
		[[nodiscard]] T* FindComponent()
		{
			if (T* found = GetComponent<T>())
				return found;

			for (GameObject* child : m_children)
				if (T* found = child->FindComponent<T>())
					return found;

			return nullptr;
		};

		// Read-only views of the hierarchy, for tooling that draws it (the editor).
		const std::vector<GameObject*>& GetChildren() const { return m_children; };
		const std::vector<ComponentBase*>& GetComponents() const { return m_components; };
		GameObject* GetParent() const { return parent; };

		// Removes one component instance. By instance rather than by type: a
		// GameObject is free to carry two of the same kind.
		void DetachComponent(ComponentBase* component);

		void RemoveChild(GameObject* gameObject);

		int GetThreadID() const { return m_threadID; };
		void SetThreadID(int thread_id);

		bool InheritsThreadID() const { return m_inherit_thread_id; };
		void SetInheritThreadID(bool inherit);

		void Destroy();

		GameObject* AddChild(const std::string& name = "New GameObject");

		// Moves this object under parent, in front of before, or last when before
		// is null or not one of parent's children. Deferred like AddChild, and
		// ignored for a root, a move that would put an object under itself, or
		// placing it before itself.
		void SetParent(GameObject* parent, const GameObject* before = nullptr);

		static size_t GetObjectCount() { return num_objects.load(); };

		// Scale, then rotation, then translation, relative to the parent.
		[[nodiscard]] glm::mat4 LocalMatrix() const;

		// Every ancestor's local matrix applied over this one's.
		[[nodiscard]] glm::mat4 WorldMatrix() const;

	protected:
		friend struct Scene;
		friend struct Engine;

		GameObject(GameObject* parent, const std::string& name, const int& thread_id);

		virtual ~GameObject();

		static inline std::atomic<size_t> num_objects = 0;
		static inline std::atomic<size_t> id_counter = 0;

		Serial<int> m_threadID;
		Serial<bool> m_inherit_thread_id;

	public:
		// After the thread fields, since a field is its position in the scene
		// file and scenes saved before it existed number those first.
		Transform transform;

	protected:
		std::vector<GameObject*>	m_children{ };
		std::vector<ComponentBase*> m_components{ };
		std::vector<ComponentBase*> m_updateables{ };
		std::vector<ComponentBase*> m_renderables{ };
		std::vector<ComponentBase*> m_physicsables{ };

	private:
		GameObject* parent;

		char newName[128];

		void Update(const int& thread);
		void Render();
		void Physics();
		void Gui();
	};
};

export module GameObject;

import Component;

import <mutex>;
import <string>;
import <vector>;


namespace Loom
{
	export struct GameObject final
	{
		GameObject(const char* name) :
			name(name)
		{
			newName[0] = '\0';
		};

		template <typename T>
		T* Attach(auto&&... args)
		{
			static_assert(std::is_base_of_v<Component, T>, "Must be a component");

			std::lock_guard lock{ mutex };

			return static_cast<T*>(components.emplace_back(new T(args...)));
		};

		GameObject* AddChild(const char* name);

	protected:
		friend struct Scene;
		friend struct Engine;

		GameObject() :
			name()
		{ };

		~GameObject()
		{
			for (auto& child : children)
				delete child;

			for (auto& component : components)
				delete component;
		};

		const size_t id = id_counter++;

		std::vector<GameObject*> children{ };
		std::vector<Component*> components{ };
		static inline std::recursive_mutex mutex;

	private:
		char newName[128];
		const char* name;

		static inline std::atomic<size_t> id_counter = 0;
		
		virtual void Update() { };
		virtual void _Update();
		
		virtual void Gui() { };
		virtual void _Gui();
	};
};

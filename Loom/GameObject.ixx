export module GameObject;

import DataTypes;

import std;
import Transform;
import Engine;
import ComponentBase;
import UI_Able;

import <atomic>;
import <memory>;


namespace Loom
{
	export struct GameObject
	{
		virtual void Gui();

		const size_t id = Engine::id_counter++;

		const auto& GetChildren() { return children; };

		static inline std::shared_ptr<GameObject>& Create(std::shared_ptr<GameObject> parent) noexcept;

		std::vector<std::shared_ptr<UI_Able>> ui_elements{ };
		std::vector<std::shared_ptr<ComponentBase>> components{ };
		std::vector<std::shared_ptr<GameObject>> children{ };
	};
};

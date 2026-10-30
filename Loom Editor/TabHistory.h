#pragma once

#include <functional>
#include <optional>
#include <vector>


namespace Loom
{
	/**
	* Loom::TabHistory
	* - The panels focused so far, walked browser style by the mouse's side
	*   buttons: focusing one after going back drops what lay ahead
	* - A panel closed since it was focused is skipped, and stays in the
	*   history in case it opens again
	*/
	struct TabHistory final
	{
		using Tab = unsigned int;
		using IsOpen = std::function<bool(Tab)>;

		static constexpr int back_button = 3;
		static constexpr int forward_button = 4;
		static constexpr int max_steps = 64;

		// Once a frame, after the panels are drawn. navigate false leaves the
		// side buttons to something else, while focus is still recorded.
		void Update(bool navigate);

		void Focus(Tab tab);

		// The tab to focus, if there is one to go to.
		std::optional<Tab> Back(const IsOpen& is_open);
		std::optional<Tab> Forward(const IsOpen& is_open);

	private:
		std::optional<Tab> Step(int direction, const IsOpen& is_open);

		std::vector<Tab> m_tabs{ };
		int m_current = 0;
	};
};

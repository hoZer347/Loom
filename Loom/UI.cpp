#include "UI.h"

#include <vector>
#include <mutex>


namespace Loom
{
	static inline std::mutex outgoing_mut;
	static inline std::vector<void(*)()> ui_elements;

	void AddUI(void(*ui_element)())
	{
		std::thread([ui_element]()
		{
			std::lock_guard<std::mutex> lock(outgoing_mut);

			ui_elements.push_back(ui_element);
		}).detach();
	};

	void RmvUI(void(*ui_element)())
	{
		std::thread([ui_element]()
		{
			std::lock_guard<std::mutex> lock(outgoing_mut);

			ui_elements.push_back(ui_element);
		}).detach();
	};

	void UpdateUI()
	{
		outgoing_mut.lock();

		for (auto& ui : ui_elements)
			ui();

		outgoing_mut.unlock();
	};
};

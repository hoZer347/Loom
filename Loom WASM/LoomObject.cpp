#include "LoomObject.h"

#include "Engine.h"

#include <string>
#include <iostream>


namespace Loom
{
	const std::string& LoomObject::GetName()
	{
		std::lock_guard lock{ mutex };

		return m_name;
	};

	void LoomObject::SetName(const std::string& name)
	{
		std::lock_guard lock{ mutex };

		this->m_name = name;

		if (by_name.contains(NameAndID()))
			by_name.erase(NameAndID());

		by_name[NameAndID()] = this;
	};

	uint64_t LoomObject::Initialize()
	{
		const uint64_t ID = Engine::GetUniqueID();

		Engine::QueueTask(
			[this]()
			{
				std::lock_guard lock{ mutex };

				if (by_name.contains(NameAndID()))
					return;

				by_name.emplace(m_name, this);
				by_id.emplace(m_ID, this);
			});

		return ID;
	};

	LoomObject* LoomObject::_GetByID(const uint64_t& ID)
	{
		std::lock_guard lock{ mutex };

		return by_id[ID];
	};

	LoomObject* LoomObject::_GetByName(const std::string& name)
	{
		std::lock_guard lock{ mutex };

		return by_name[name];
	};

	constexpr std::string LoomObject::NameAndID() const
	{
		return m_name + " (ID: " + std::to_string(m_ID) + ')';
	};

	LoomObject::~LoomObject()
	{
		std::lock_guard lock{ mutex };

		by_id.erase(m_ID);
		by_name.erase(m_name);
	};
};

#include "LoomObject.h"

#include "Engine.h"

#include <algorithm>
#include <string>
#include <iostream>


namespace Loom
{
	namespace
	{
		// The last object whose constructor started on this thread, which is the
		// one a Serial member being built right now belongs to. There is nowhere
		// to clear it when a constructor finishes, so it goes on naming that
		// object until the next one is built or that one is destroyed.
		thread_local LoomObject* constructing = nullptr;
	};

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

	void LoomObject::SetGuid(const Guid& guid)
	{
		std::lock_guard lock{ mutex };

		if (guid == m_guid)
			return;

		const auto ours = by_guid.find(m_guid);

		if (ours != by_guid.end() && ours->second == this)
			by_guid.erase(ours);

		// A scene file is the authority on identity, so an object already
		// answering to the incoming guid gives it up rather than sharing it.
		const auto incumbent = by_guid.find(guid);

		if (incumbent != by_guid.end() && incumbent->second != this)
			by_guid.erase(incumbent);

		m_guid = guid;

		if (m_guid.IsValid())
			by_guid[m_guid] = this;
	};

	uint64_t LoomObject::Initialize()
	{
		// Runs before any member of a derived class is built, which is what puts
		// the Serial members those declare on this object.
		constructing = this;

		const uint64_t ID = Engine::GetUniqueID();

		{
			// Registered straight away rather than on the task queue: loading a
			// scene resolves references between objects as it builds them, and
			// cannot wait a frame for them to become findable.
			std::lock_guard lock{ mutex };

			by_guid[m_guid] = this;
		};

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

	void RegisterSerialField(
		FieldType type,
		const char* name,
		void* data,
		LoomObject* (*get_reference)(void*),
		void (*set_reference)(void*, LoomObject*))
	{
		LoomObject* owner = constructing;

		if (owner == nullptr)
		{
			std::cerr
				<< "A Serial field has no LoomObject under construction on this"
				   " thread to belong to" << std::endl;

			return;
		};

		// Under the lock like the lookup tables: a destructor elsewhere walks the
		// referencing list, and objects are made wherever their owner happens to run.
		std::lock_guard lock{ LoomObject::mutex };

		SerializedField field;

		field.type = type;
		field.name = name;
		field.data = data;
		field.get_reference = get_reference;
		field.set_reference = set_reference;

		// Listed once, however many reference fields the object goes on to declare.
		if (type == FieldType::Reference && !owner->m_references)
		{
			owner->m_references = true;

			LoomObject::referencing.push_back(owner);
		};

		owner->m_fields.push_back(field);
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

	LoomObject* LoomObject::_GetByGuid(const Guid& guid)
	{
		std::lock_guard lock{ mutex };

		const auto found = by_guid.find(guid);

		return found == by_guid.end()
			? nullptr
			: found->second;
	};

	std::string LoomObject::NameAndID() const
	{
		return m_name + " (ID: " + std::to_string(m_ID) + ')';
	};

	LoomObject::~LoomObject()
	{
		std::lock_guard lock{ mutex };

		// So a field declared after this point is refused rather than handed an
		// object that is already gone.
		if (constructing == this)
			constructing = nullptr;

		by_id.erase(m_ID);
		by_name.erase(m_name);

		// Only while it is still ours: SetGuid hands a guid over to whoever
		// loads a scene that claims it, and that object outlives this one.
		const auto entry = by_guid.find(m_guid);

		if (entry != by_guid.end() && entry->second == this)
			by_guid.erase(entry);

		// Nobody may be left pointing here. A Reference field still holding this
		// object would be read again next frame - by the serializer writing its
		// guid, by the inspector drawing its name, or by whatever component owns
		// the field - and by then the memory is gone.
		for (LoomObject* holder : referencing)
			if (holder != this)
				for (const SerializedField& field : holder->m_fields)
					if (field.type == FieldType::Reference && field.GetReference() == this)
						field.SetReference(nullptr);

		if (m_references)
			referencing.erase(
				std::remove(referencing.begin(), referencing.end(), this),
				referencing.end());
	};
};

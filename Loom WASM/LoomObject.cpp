#include "LoomObject.h"

#include "Engine.h"

#include <atomic>
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

		Touch();
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

		Touch();
	};

	void LoomObject::Touch(bool placed)
	{
		if (!watching)
			return;

		const unsigned char kinds = placed ? touched_placed : touched_self;

		// Already in for this: whoever takes the journal reads the object
		// after taking it, so sees this change too.
		if ((m_touched & kinds) == kinds)
			return;

		std::lock_guard lock{ journal_mutex };

		if (m_touched == 0)
		{
			m_journalAt = journal.size();
			journal.push_back(this);
		};

		m_touched |= kinds;
	};

	void LoomObject::Watch(bool watch)
	{
		std::lock_guard lock{ journal_mutex };

		if (watch == watching)
			return;

		watching = watch;

		if (watch)
		{
			watch_count++;

			return;
		};

		for (LoomObject* object : journal)
			object->m_touched = 0;

		journal.clear();
		destroyed.clear();
	};

	LoomObject::Journal LoomObject::TakeJournal()
	{
		std::lock_guard lock{ journal_mutex };

		Journal taken{ };

		taken.touched.reserve(journal.size());

		for (LoomObject* object : journal)
		{
			taken.touched.push_back({ object, (object->m_touched & touched_self) != 0, (object->m_touched & touched_placed) != 0 });
			object->m_touched = 0;
		};

		journal.clear();
		taken.destroyed.swap(destroyed);

		return taken;
	};

	void SerialChanged(LoomObject* owner, bool reference)
	{
		if (owner == nullptr)
			return;

		owner->Touch();

		if (reference)
			owner->ReferencesWritten();
	};

	void LoomObject::ReferencesWritten()
	{
		// Still waiting: Reindex clears this before it reads the fields, so it
		// reads this write too.
		if (m_referencesDirty)
			return;

		std::lock_guard lock{ mutex };

		if (m_referencesDirty)
			return;

		m_referencesDirty = true;
		m_dirtyAt = dirty_holders.size();
		dirty_holders.push_back(this);
	};

	void LoomObject::Reindex()
	{
		std::lock_guard lock{ mutex };

		for (LoomObject* holder : dirty_holders)
		{
			holder->m_referencesDirty = false;

			for (const LoomObject* target : holder->m_referenced)
			{
				const auto entry = referrers.find(target);

				entry->second.erase(holder);

				if (entry->second.empty())
					referrers.erase(entry);
			};

			holder->m_referenced.clear();

			for (const SerializedField& field : holder->m_fields)
			{
				LoomObject* target = field.type == FieldType::Reference ? field.GetReference() : nullptr;

				if (target == nullptr || target == holder)
					continue;

				if (referrers[target].insert(holder).second)
					holder->m_referenced.push_back(target);
			};
		};

		dirty_holders.clear();
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

	LoomObject* RegisterSerialField(
		FieldType type,
		void* data,
		LoomObject* (*get_reference)(void*),
		void (*set_reference)(void*, LoomObject*),
		bool (*accepts_reference)(LoomObject*),
		const char* name,
		unsigned long long* version)
	{
		// Each field's count starts this far past the last one's, further than
		// any field is ever changed.
		constexpr int VERSIONS_EACH = 32;
		static std::atomic<unsigned long long> fields{ 0 };

		*version = fields.fetch_add(1) << VERSIONS_EACH;

		LoomObject* owner = constructing;

		if (owner == nullptr)
		{
			std::cerr
				<< "A Serial field has no LoomObject under construction on this"
				   " thread to belong to" << std::endl;

			return nullptr;
		};

		// Under the lock like the lookup tables: a destructor elsewhere reads
		// the fields of whoever points at it, and objects are made wherever
		// their owner happens to run.
		std::lock_guard lock{ LoomObject::mutex };

		SerializedField field;

		field.type = type;
		field.data = data;
		field.get_reference = get_reference;
		field.set_reference = set_reference;
		field.accepts_reference = accepts_reference;
		field.version = version;
		field.owner = owner;
		field.name = owner->TakeName(name);

		owner->m_fields.push_back(field);

		if (type == FieldType::Reference)
			owner->ReferencesWritten();

		return owner;
	};

	void RegisterButton(const char* name, const std::function<void()>* press)
	{
		LoomObject* owner = constructing;

		if (owner == nullptr)
		{
			std::cerr
				<< "A Button has no LoomObject under construction on this"
				   " thread to belong to" << std::endl;

			return;
		};

		owner->m_buttons.push_back({ owner->TakeName(name), press, owner->m_fields.size() });
	};

	std::string LoomObject::TakeName(const char* given) const
	{
		std::string name = given;

		if (!name.starts_with(previous_category))
			return name;

		// The last Button, if no field was declared after it, or else the last field.
		const bool afterButton = !m_buttons.empty() && m_buttons.back().fieldsBefore == m_fields.size();
		const std::string_view category =
			afterButton ? NameCategory(m_buttons.back().name) :
			m_fields.empty() ? std::string_view() :
			m_fields.back().Category();
		const std::string display = name.substr(previous_category.size());

		return category.empty() ? display : std::string(category) + '/' + display;
	};

	LoomObject* LoomObject::_GetByID(const uint64_t& ID)
	{
		std::lock_guard lock{ mutex };

		const auto found = by_id.find(ID);

		return found == by_id.end()
			? nullptr
			: found->second;
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

		{
			std::lock_guard journalLock{ journal_mutex };

			if (m_touched != 0)
			{
				LoomObject* last = journal.back();

				journal[m_journalAt] = last;
				last->m_journalAt = m_journalAt;
				journal.pop_back();
			};

			if (watching)
				destroyed.push_back(this);
		};

		if (m_referencesDirty)
		{
			LoomObject* last = dirty_holders.back();

			dirty_holders[m_dirtyAt] = last;
			last->m_dirtyAt = m_dirtyAt;
			dirty_holders.pop_back();
		};

		// What this pointed at is taken out as it was indexed, since the
		// fields that said so may be gone by now.
		for (const LoomObject* target : m_referenced)
		{
			const auto entry = referrers.find(target);

			entry->second.erase(this);

			if (entry->second.empty())
				referrers.erase(entry);
		};

		Reindex();

		const auto holders = referrers.find(this);

		if (holders == referrers.end())
			return;

		// Nobody may be left pointing here. A Reference field still holding this
		// object would be read again next frame - by the serializer writing its
		// guid, by the inspector drawing its name, or by whatever component owns
		// the field - and by then the memory is gone.
		for (LoomObject* holder : std::unordered_set<LoomObject*>(std::move(holders->second)))
		{
			for (const SerializedField& field : holder->m_fields)
				if (field.type == FieldType::Reference && field.GetReference() == this)
					field.SetReference(nullptr);

			std::erase(holder->m_referenced, this);
		};

		referrers.erase(this);
	};
};

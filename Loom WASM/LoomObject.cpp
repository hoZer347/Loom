#include "LoomObject.h"

#include "Engine.h"

#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <iostream>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		// The object whose constructor last started on this thread, owner of any Serial being built.
		thread_local LoomObject* constructing = nullptr;

		// What a Serial needs but does not hold, kept by the Serial's address.
		struct SerialRecord
		{
			LoomObject* owner = nullptr;
			unsigned long long version = 0;
		};

		// Several, so threads writing different fields rarely meet.
		struct SerialShard
		{
			std::shared_mutex mutex{ };
			std::unordered_map<const void*, SerialRecord> records{ };
		};

		constexpr size_t SERIAL_SHARDS = 64;

		std::array<SerialShard, SERIAL_SHARDS> serial_shards{ };

		SerialShard& ShardOf(const void* data)
		{
			return serial_shards[std::hash<const void*>{ }(data) % SERIAL_SHARDS];
		};
	};

	#pragma region Identity

	const std::string& LoomObject::GetName()
	{
		std::lock_guard lock{ mutex };

		return m_name;
	};

	void LoomObject::SetName(const std::string& name)
	{
		std::lock_guard lock{ mutex };

		Unname();
		m_name = name;
		by_name[m_name].insert(this);

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

		// The scene file is the authority, so an object holding the guid gives it up.
		const auto incumbent = by_guid.find(guid);

		if (incumbent != by_guid.end() && incumbent->second != this)
			by_guid.erase(incumbent);

		m_guid = guid;

		if (m_guid.IsValid())
			by_guid[m_guid] = this;

		Touch();
	};

	uint64_t LoomObject::Initialize()
	{
		// Before any derived member is built, so their Serial members land on this object.
		constructing = this;

		const uint64_t ID = Engine::GetUniqueID();

		{
			// Registered now, since loading resolves references as it builds.
			std::lock_guard lock{ mutex };

			by_id.emplace(ID, this);
			by_guid[m_guid] = this;
		};

		return ID;
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

		const auto found = by_name.find(name);

		return found == by_name.end()
			? nullptr
			: *found->second.begin();
	};

	void LoomObject::Unname()
	{
		const auto found = by_name.find(m_name);

		if (found == by_name.end())
			return;

		found->second.erase(this);

		if (found->second.empty())
			by_name.erase(found);
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

	#pragma endregion

	#pragma region Fields

	void RegisterSerialField(
		FieldType type,
		void* data,
		LoomObject* (*get_reference)(void*),
		void (*set_reference)(void*, LoomObject*),
		bool (*accepts_reference)(LoomObject*),
		const char* name,
		std::span<const char* const> extensions)
	{
		// Each field's count starts this far past the last, beyond any field's changes.
		constexpr int VERSIONS_EACH = 32;
		static std::atomic<unsigned long long> fields{ 0 };

		LoomObject* owner = constructing;

		if (owner == nullptr)
		{
			std::cerr
				<< "A Serial field has no LoomObject under construction on this"
				   " thread to belong to" << std::endl;

			return;
		};

		SerialShard& shard = ShardOf(data);
		unsigned long long* version = nullptr;

		{
			std::unique_lock lock{ shard.mutex };

			SerialRecord& record = shard.records[data];

			record = { owner, fields.fetch_add(1) << VERSIONS_EACH };
			version = &record.version;
		};

		// Under the lock, since destructors elsewhere read their holders' fields.
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

		field.extensions.assign(extensions.begin(), extensions.end());

		owner->m_fields.push_back(field);

		if (type == FieldType::Reference)
			owner->ReferencesWritten();
	};

	unsigned long long LoomObject::VersionOf(const void* serial) const
	{
		for (const SerializedField& field : m_fields)
			if (field.data == serial)
				return *field.version;

		return 0;
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

	LoomObject* ObjectUnderConstruction()
	{
		return constructing;
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

	#pragma endregion

	#pragma region Journal

	void LoomObject::Touch(bool placed)
	{
		if (!watching)
			return;

		const unsigned char kinds = placed ? touched_placed : touched_self;

		// Already in: the journal's reader sees this change too.
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

	void SerialChangedAt(const void* data, bool reference, bool counted)
	{
		if (!counted && !LoomObject::watching)
			return;

		SerialShard& shard = ShardOf(data);
		LoomObject* owner = nullptr;

		{
			std::shared_lock lock{ shard.mutex };

			const auto found = shard.records.find(data);

			if (found == shard.records.end())
				return;

			found->second.version++;
			owner = found->second.owner;
		};

		SerialChanged(owner, reference);
	};

	#pragma endregion

	#pragma region References

	void LoomObject::ReferencesWritten()
	{
		// Still pending: Reindex clears this before reading, so it sees this write.
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

	#pragma endregion

	LoomObject::~LoomObject()
	{
		std::lock_guard lock{ mutex };

		// So a field declared after this is refused rather than handed a dead object.
		if (constructing == this)
			constructing = nullptr;

		by_id.erase(m_ID);
		Unname();

		// Only while ours, since SetGuid can hand it to a loaded object.
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

		for (const SerializedField& field : m_fields)
		{
			SerialShard& shard = ShardOf(field.data);
			std::unique_lock lock{ shard.mutex };

			const auto found = shard.records.find(field.data);

			if (found != shard.records.end() && found->second.owner == this)
				shard.records.erase(found);
		};

		if (m_referencesDirty)
		{
			LoomObject* last = dirty_holders.back();

			dirty_holders[m_dirtyAt] = last;
			last->m_dirtyAt = m_dirtyAt;
			dirty_holders.pop_back();
		};

		// Taken out as indexed, since the fields may be gone.
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

		// No Reference field may still point here once the memory is gone.
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

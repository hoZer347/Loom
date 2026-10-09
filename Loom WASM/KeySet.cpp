#include "KeySet.h"

#include <cstdint>


namespace Loom
{
	namespace
	{
		// Grown to keep at least this many slots for each key, from this
		// many; a key's two addresses mixed by two large odd numbers, keeping
		// the high bits, which they stir the most.
		constexpr size_t MOST_FULL = 2;
		constexpr size_t LEAST_SLOTS = 1024;
		constexpr uint64_t FIRST_MIX = 0x9E3779B97F4A7C15ull;
		constexpr uint64_t SECOND_MIX = 0xC2B2AE3D27D4EB4Full;
		constexpr int HASH_SHIFT = 32;
	};

	bool KeySet::Insert(const Key& key)
	{
		if ((m_count + 1) * MOST_FULL > m_slots.size())
			Grow();

		const size_t mask = m_slots.size() - 1;

		for (size_t slot = Home(key); ; slot = (slot + 1) & mask)
		{
			if (m_slots[slot] == key)
				return false;

			if (m_slots[slot].first == nullptr)
			{
				m_slots[slot] = key;
				m_count++;

				return true;
			};
		};
	};

	void KeySet::Erase(const Key& key)
	{
		if (m_slots.empty())
			return;

		const size_t mask = m_slots.size() - 1;
		size_t hole = Home(key);

		for (; m_slots[hole] != key; hole = (hole + 1) & mask)
			if (m_slots[hole].first == nullptr)
				return;

		m_count--;

		// Those after it that would rather sit in the hole move back into it,
		// so none is left past a gap from where it belongs.
		for (size_t next = (hole + 1) & mask; m_slots[next].first != nullptr; next = (next + 1) & mask)
		{
			const size_t home = Home(m_slots[next]);
			const bool between = hole <= next ? hole < home && home <= next : hole < home || home <= next;

			if (!between)
			{
				m_slots[hole] = m_slots[next];
				hole = next;
			};
		};

		m_slots[hole] = Key{ };
	};

	size_t KeySet::Home(const Key& key) const
	{
		const uint64_t mixed = (uint64_t)(uintptr_t)key.first * FIRST_MIX ^ (uint64_t)(uintptr_t)key.second * SECOND_MIX;

		return (size_t)(mixed >> HASH_SHIFT) & (m_slots.size() - 1);
	};

	void KeySet::Grow()
	{
		std::vector<Key> old = std::move(m_slots);

		m_slots.assign(std::max(old.size() * 2, LEAST_SLOTS), Key{ });
		m_count = 0;

		for (const Key& key : old)
			if (key.first != nullptr)
				Insert(key);
	};
};

#pragma once

#include "Loom API.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>


namespace Loom
{
	struct Collider;

	/**
	* Loom::KeySet
	* - A set of pairs of colliders in one array, open addressed, which the
	*   thousands of lookups a physics step makes go through far faster than
	*   through a node each
	* - A pair is kept as given: whoever builds the keys orders each one's two
	*/
	struct LOOM_API KeySet final
	{
		typedef std::pair<const Collider*, const Collider*> Key;

		// False for one already in.
		bool Insert(const Key& key);
		void Erase(const Key& key);

		template <typename Doomed>
		void EraseIf(const Doomed& doomed)
		{
			std::vector<Key> kept{ };

			for (const Key& key : m_slots)
				if (key.first != nullptr && !doomed(key))
					kept.push_back(key);

			std::fill(m_slots.begin(), m_slots.end(), Key{ });
			m_count = 0;

			for (const Key& key : kept)
				Insert(key);
		};

		size_t Size() const { return m_count; };

	private:
		// Where key would sit with nothing in its way.
		size_t Home(const Key& key) const;
		void Grow();

		// Empty ones hold nulls.
		std::vector<Key> m_slots{ };
		size_t m_count = 0;
	};
};

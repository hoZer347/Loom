#include "doctest.h"

#include "KeySet.h"

#include <cstdint>
#include <random>
#include <set>


namespace
{
	// Keys from numbers: the set only ever compares and hashes the addresses.
	Loom::KeySet::Key KeyOf(uintptr_t first, uintptr_t second)
	{
		constexpr uintptr_t ALIGNED = sizeof(void*);

		return { reinterpret_cast<const Loom::Collider*>((first + 1) * ALIGNED), reinterpret_cast<const Loom::Collider*>((second + 1) * ALIGNED) };
	};
};

TEST_SUITE("KeySet")
{
	TEST_CASE("keys go in once and come out, through thousands put in and taken out in mixed order")
	{
		// Enough keys that the set grows several times, and clusters of them
		// run on past the end of its array.
		constexpr int ROUNDS = 40000;
		constexpr uintptr_t COLLIDERS = 120;
		constexpr unsigned SEED = 3;

		// More put in than taken out, so the set fills.
		constexpr double PUTTING = 0.6;

		std::mt19937 random(SEED);
		std::uniform_int_distribution<uintptr_t> collider(0, COLLIDERS - 1);
		std::bernoulli_distribution putting(PUTTING);

		Loom::KeySet set;
		std::set<Loom::KeySet::Key> expected;
		int mismatches = 0;

		for (int round = 0; round < ROUNDS; round++)
		{
			const Loom::KeySet::Key key = KeyOf(collider(random), collider(random));

			if (putting(random))
				mismatches += set.Insert(key) != expected.insert(key).second;
			else
			{
				set.Erase(key);
				expected.erase(key);
			};
		};

		CHECK(mismatches == 0);
		CHECK(set.Size() == expected.size());

		// Each one in is found there, and none left out is.
		int lost = 0;

		for (const Loom::KeySet::Key& key : expected)
			lost += set.Insert(key);

		for (uintptr_t first = 0; first < COLLIDERS; first++)
			for (uintptr_t second = 0; second < COLLIDERS; second++)
			{
				const Loom::KeySet::Key key = KeyOf(first, second);

				if (expected.count(key) == 0)
				{
					lost += !set.Insert(key);
					set.Erase(key);
				};
			};

		CHECK(lost == 0);
		CHECK(set.Size() == expected.size());
	};

	TEST_CASE("EraseIf takes out what it is told to and keeps the rest")
	{
		constexpr uintptr_t COLLIDERS = 40;
		constexpr uintptr_t DOOMED = 7;

		Loom::KeySet set;

		for (uintptr_t first = 0; first < COLLIDERS; first++)
			for (uintptr_t second = first + 1; second < COLLIDERS; second++)
				set.Insert(KeyOf(first, second));

		const Loom::KeySet::Key doomed = KeyOf(DOOMED, DOOMED);
		set.EraseIf([&](const Loom::KeySet::Key& key) { return key.first == doomed.first || key.second == doomed.first; });

		size_t kept = 0;

		for (uintptr_t first = 0; first < COLLIDERS; first++)
			for (uintptr_t second = first + 1; second < COLLIDERS; second++)
			{
				const bool involved = first == DOOMED || second == DOOMED;
				const bool absent = set.Insert(KeyOf(first, second));

				CHECK(absent == involved);
				kept += !absent;
			};

		CHECK(kept == (COLLIDERS - 1) * (COLLIDERS - 2) / 2);
	};
};

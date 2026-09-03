#pragma once

#include "Loom API.h"

#include <cstdint>
#include <functional>
#include <string>


namespace Loom
{
	/**
	* Loom::Guid
	* - A 128 bit identity that survives being written to disk and read back
	* - Scene files point at objects with these rather than with pointers or the
	*   per-run counter behind LoomObject::m_ID, neither of which mean anything
	*   the next time the program starts
	*/
	struct LOOM_API Guid final
	{
		Guid() = default;

		Guid(uint64_t high, uint64_t low) :
			high(high),
			low(low)
		{ };

		static Guid New();

		// Accepts the canonical 8-4-4-4-12 form, and the same digits without the
		// dashes. Leaves out untouched and answers false for anything else.
		static bool TryParse(const std::string& text, Guid& out);

		std::string ToString() const;

		bool IsValid() const { return high != 0 || low != 0; };

		bool operator==(const Guid& rhs) const { return high == rhs.high && low == rhs.low; };
		bool operator!=(const Guid& rhs) const { return !(*this == rhs); };

		uint64_t high = 0;
		uint64_t low = 0;
	};
};


template <>
struct std::hash<Loom::Guid>
{
	size_t operator()(const Loom::Guid& guid) const noexcept
	{
		return (size_t)(guid.high ^ (guid.low * 1099511628211ull));
	};
};

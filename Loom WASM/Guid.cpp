#include "Guid.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <random>


namespace Loom
{
	Guid Guid::New()
	{
		// One generator for the process, seeded once. Guids are handed out from
		// whatever thread creates an object, so the draw is locked.
		static std::mutex mutex;
		static std::mt19937_64 generator(std::random_device{ }());

		std::scoped_lock lock(mutex);

		std::uniform_int_distribution<uint64_t> distribution;

		Guid guid(distribution(generator), distribution(generator));

		// Version 4 (random) and the RFC 4122 variant, so these are recognisable
		// as guids to anything else that reads the file.
		guid.high = (guid.high & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
		guid.low = (guid.low & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;

		return guid;
	};

	std::string Guid::ToString() const
	{
		constexpr size_t textLength = 36;

		char buffer[textLength + 1]{ };

		snprintf(
			buffer,
			sizeof(buffer),
			"%08x-%04x-%04x-%04x-%012llx",
			(unsigned int)(high >> 32),
			(unsigned int)((high >> 16) & 0xFFFF),
			(unsigned int)(high & 0xFFFF),
			(unsigned int)(low >> 48),
			(unsigned long long)(low & 0x0000FFFFFFFFFFFFull));

		return buffer;
	};

	bool Guid::TryParse(const std::string& text, Guid& out)
	{
		// The dashed form, 8-4-4-4-12, or the same digits bare.
		constexpr size_t digits = 32;
		constexpr size_t dashes[] = { 8, 13, 18, 23 };

		const bool dashed = text.size() == digits + std::size(dashes);

		if (!dashed && text.size() != digits)
			return false;

		std::string hex;
		hex.reserve(digits);

		for (size_t at = 0; at < text.size(); at++)
		{
			const char c = text[at];

			if (dashed && std::find(std::begin(dashes), std::end(dashes), at) != std::end(dashes))
			{
				if (c != '-')
					return false;

				continue;
			};

			if (!isxdigit((unsigned char)c))
				return false;

			hex += c;
		};

		out = Guid(
			strtoull(hex.substr(0, 16).c_str(), nullptr, 16),
			strtoull(hex.substr(16, 16).c_str(), nullptr, 16));

		return true;
	};
};

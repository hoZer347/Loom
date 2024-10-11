#pragma once

namespace Loom
{
	template<size_t N>
	struct Str
	{
		constexpr Str(const char(&str)[N])
		{
			std::copy_n(str, N, value);
		};
		char value[N];
	};
};

#include "Macro Helpers.h"

export module Timer;

import <iostream>;
import <chrono>;


namespace Loom
{
	////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// Str: A template struct that holds a string literal
	// - Example:
	//		Str<"Hello, World!"> str;
	//		std::cout << str.value; // Hello, World!
	//
	export template<size_t N>
		struct Str final
	{
		constexpr Str(const char(&str)[N])
		{
			std::copy_n(str, N, value);
		};
		char value[N];
	};
	//
	////////////////////////////////////////////////////////////////////////////////////////////////////

	template <Str m_name = "Default">
	struct Timer
	{
	protected:
		template <Str> friend inline void StartTimer();
		template <Str> friend inline int64_t GetTimeSince();
		static inline int64_t start_time{ };
	};

	export template <Str m_name = "Default">
		inline void StartTimer()
	{
		Timer<m_name>::start_time = std::chrono::system_clock::now().time_since_epoch().count();
	};

	export template <Str m_name = "Default">
		inline int64_t GetTimeSince()
	{
		return std::chrono::system_clock::now().time_since_epoch().count() - Timer<m_name>::start_time;
	};

	export template <Str m_name = "Default">
		inline void PrintTimeElapsed()
	{
		auto timeElapsed = GetTimeSince<m_name>();
		//TODO: implement logging
		std::cout << "Time Elapsed: " << timeElapsed << std::endl;
	};
};

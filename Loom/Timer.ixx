export module Timer;

import <chrono>;
import <iostream>;


namespace Loom
{
	export template<size_t N>
		struct Str final
	{
		constexpr Str(const char(&str)[N])
		{
			std::copy_n(str, N, value);
		};
		char value[N];
	};

	template <Str name = "Default">
	struct Timer
	{
	protected:
		template <Str> friend inline void StartTimer();
		template <Str> friend inline int64_t GetTimeSince();
		static inline int64_t start_time{ };
	};

	export template <Str name = "Default">
		inline void StartTimer()
	{
		Timer<name>::start_time = std::chrono::system_clock::now().time_since_epoch().count();
	};

	export template <Str name = "Default">
		inline int64_t GetTimeSince()
	{
		return std::chrono::system_clock::now().time_since_epoch().count() - Timer<name>::start_time;
	};

	export template <Str name = "Default">
		inline void PrintTimeElapsed()
	{
		auto timeElapsed = GetTimeSince<name>();
		//TODO: implement logging
		std::cout << "Time Elapsed: " << timeElapsed << std::endl;
	};
};

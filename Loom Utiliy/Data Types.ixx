export module DataTypes;

import std;
import <vector>;
import <memory>;
import <string>;
import <mutex>;
import <iostream>;
import <fstream>;
import "Definitions.hpp";


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


	export template <typename T>
		struct alignas(sizeof(uint64_t)) ptr final
	{
		constexpr ptr(auto&&... args) noexcept
		{
			std::lock_guard lock{ s_mutex };

			// TODO: implement logging
			std::cout << "Creating " << typeid(T).name()  << std::endl;

			m_data_index = s_data.size();
			s_data.emplace_back(args...);
			s_ptrs.emplace_back(this);
		};
		
		constexpr ~ptr() noexcept
		{
			//std::lock_guard lock{ s_mutex };

			//// TODO: implement logging
			//std::cout << "Deleting " << typeid(T).name() << std::endl;

			//std::swap(s_data[m_data_index], s_data.back());
			//std::swap(s_ptrs[m_data_index], s_ptrs.back());
			//
			//s_ptrs[m_data_index]->m_data_index = m_data_index;

			//s_data.pop_back();
			//s_ptrs.pop_back();
		};

		static constexpr void lock() noexcept
		{
			s_mutex.lock();
		};

		static constexpr void unlock() noexcept
		{
			s_mutex.unlock();
		};

		static constexpr void allocate(const auto& amount) noexcept
		{
			std::lock_guard lock{ s_mutex };

			s_data.reserve(s_data.resize(s_data.size() + amount));
			s_ptrs.reserve(s_ptrs.resize(s_ptrs.size() + amount));
		};

		_NODISCARD constexpr T* operator->() const noexcept
		{
			return &s_data[m_data_index];
		};

		_NODISCARD constexpr operator T& ()
		{
			return s_data[m_data_index];
		};

	//protected:
		uint64_t m_data_index;

		static inline std::vector<T> s_data{ };
		static inline std::vector<ptr<T>*> s_ptrs{ };

		static inline std::mutex s_mutex{ };
	};

	template <typename T>
	struct Chunk final
	{

	};
};

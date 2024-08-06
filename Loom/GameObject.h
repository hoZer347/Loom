#pragma once

#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <iostream>
#include <fstream>

#define LOG_FILE_PATH "Log.txt"


namespace Loom
{
	template<typename T>
	concept _update_test = requires
	{
		{ std::declval<T>().Update() } -> std::same_as<void>;
	};

	template<typename T>
	concept _render_test = requires
	{
		{ std::declval<T>().Render() } -> std::same_as<void>;
	};

	template<typename T, typename S>
	concept _container_test = requires
	{
		{ std::declval<T>().begin() } -> std::same_as<S&>;
		{ std::declval<T>().end()	} -> std::same_as<S&>;
	};

	template <typename T>
	struct ptr final
	{
		template <bool TRACK = true>
		constexpr ptr(auto&&... args) noexcept :
			_ptr(std::make_shared<T>(std::forward<decltype(args)>(args)...))
		{ };

		constexpr auto operator->() noexcept
		{
			return _ptr.std::shared_ptr<T>::operator->();
		};
	
		static constexpr auto begin() noexcept
		{
			return allOfThem.begin();
		};

		static constexpr auto end() noexcept
		{
			return allOfThem.end();
		};

	protected:
		template <typename S, typename... T>
		friend inline constexpr void Add(
			ptr<S>& _ptr,
			ptr<T>&... _ptrs) noexcept;

		template <typename S, typename... T>
		friend inline constexpr void Rmv(
			ptr<S>& _ptr,
			ptr<T>&... _ptrs) noexcept;

		template <typename S, typename... T>
		friend inline constexpr void Destroy(
			ptr<S>& _ptr,
			ptr<T>&... _ptrs) noexcept;

		std::shared_ptr<T> _ptr;
		static inline std::recursive_mutex mut{ };
		static inline std::vector<std::shared_ptr<T>> allOfThem{ };
	};

	static inline std::ofstream f{ LOG_FILE_PATH };

	inline constexpr void Add() noexcept { };

	template <typename S, typename... T>
	inline constexpr void Add(
		ptr<S>& _ptr,
		ptr<T>&... _ptrs) noexcept
	{
		std::lock_guard<std::recursive_mutex> lock(_ptr.mut);

		Add(_ptrs...);

		_ptr.allOfThem.emplace_back(_ptr._ptr);

		f << "Added " << typeid(ptr<S>).name() << " at " << (size_t)&_ptr << std::endl;
	};

	inline constexpr void Rmv() noexcept { };

	template <typename S, typename... T>
	inline constexpr void Rmv(
		ptr<S>& _ptr,
		ptr<T>&... _ptrs) noexcept
	{
		std::lock_guard<std::recursive_mutex> lock(_ptr.mut);

		Rmv(_ptrs...);

		_ptr.allOfThem.erase(std::remove(_ptr.allOfThem.begin(), _ptr.allOfThem.end(), _ptr._ptr), _ptr.allOfThem.end());

		f << "Rmved " << typeid(ptr<S>).name() << " at " << (size_t)&_ptr << std::endl;	
	};

	inline constexpr void Destroy() noexcept { };

	template <typename S, typename... T>
	inline constexpr void Destroy(
		ptr<S>& _ptr,
		ptr<T>&... _ptrs) noexcept
	{
		std::lock_guard<std::recursive_mutex> lock(_ptr.mut);

		Destroy(_ptrs...);

		if (_ptr._ptr == nullptr)
		{
			f << "Attempted to destroy " << typeid(ptr<S>).name() << " at " << (size_t)&_ptr << " when it has already been destroyed" << std::endl;
			return;
		};

		_ptr._ptr.reset();

		f << "Destroyed " << typeid(ptr<S>).name() << " at " << (size_t)&_ptr << std::endl;
	};
};

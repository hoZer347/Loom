#include "Macro Helpers.h"

export module Serialize;

import <mutex>;
import <string>;
import <fstream>;
import <iostream>;
import <unordered_map>;

#define SAVE_STREAM std::cout


namespace Loom
{
	HAS_FUNCTION_DECL(begin);
	HAS_FUNCTION_DECL(end);

	HAS_FUNCTION_DECL(OnSerialize);

	HAS_VARIABLE_DECL(m_name);
	HAS_VARIABLE_DECL(m_id);

	export struct Serialize
	{
		Serialize() = delete;
		
		template <typename T>
		static void Register()
		{
			std::lock_guard lock{ registry_mutex };

			if (!registered_types.contains(typeid(T).name()))
				registered_types = typeid(T).name();
		};

		template <typename T>
		static void Push(T* t)
		{
			// If a collection, cascade for each item
			if constexpr (
				HAS_FUNCTION_TEST(T, begin) &&
				HAS_FUNCTION_TEST(T, end))
			{
				for (auto& i : *t)
					Push(i);

				return;
			};
			//


			// Print the name if it has one, otherwise type, along with it's ID
			if constexpr (HAS_VARIABLE_TEST(T, m_name))
				SAVE_STREAM << t->m_name << std::endl;
			else SAVE_STREAM << typeid(T).name() << std::endl;

			if constexpr (HAS_VARIABLE_TEST(T, m_id))
				SAVE_STREAM << "ID: " << t->m_id << std::endl;
			//


			// If it has a serialize function, call it
			if constexpr (HAS_FUNCTION_TEST(T, OnSerialize))
				t->OnSerialize();
			//
		};

	private:
		static inline std::unordered_map<std::string, void(*)(void*)> registered_types{ };
		static inline std::recursive_mutex registry_mutex{ };
	};
};

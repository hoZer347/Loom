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
				SAVE_STREAM << indent << t->m_name << std::endl;
			else SAVE_STREAM << indent << typeid(T).name() << std::endl;

			if constexpr (HAS_VARIABLE_TEST(T, m_id))
				SAVE_STREAM << indent << "ID: " << t->m_id << std::endl;
			//

			if constexpr (HAS_FUNCTION_TEST(T, OnSerialize))
			{
				indent += '\t';
				t->OnSerialize();
				indent.pop_back();
			};

			file.close();
			//
		};

		static void Push(const std::string string)
		{
			SAVE_STREAM << indent << string;
		};

	protected:
		friend struct MainMenu;

		static inline void SetProjectFile(std::string file_path)
		{
			if (file.is_open())
				file.close();

			file.open(file_path);
		};

	private:
		static inline thread_local std::string indent{ };
		static inline thread_local std::fstream file{ };
		static inline std::unordered_map<std::string, void(*)(void*)> registered_types{ };
		static inline std::recursive_mutex registry_mutex{ };
	};
};

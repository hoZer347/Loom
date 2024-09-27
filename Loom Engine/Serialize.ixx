#include "Macro Helpers.h"

export module Serialize;

import LoomObject;

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
		
		static void Start(const std::string& path)
		{

		};

		static void Close()
		{

		};

		template <typename T>
		static void Push(T* t)
		{
			// Print the name if it has one, otherwise type, along with its ID
			if constexpr (HAS_VARIABLE_TEST(T, m_name))
				SAVE_STREAM << t->m_name << std::endl;
			else SAVE_STREAM << typeid(T).name() << std::endl;

			if constexpr (HAS_VARIABLE_TEST(T, m_id))
				SAVE_STREAM << "ID: " << t->m_id << std::endl;
			//


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


			// If it has a serialize function, call it
			if constexpr (HAS_FUNCTION_TEST(T, OnSerialize))
				t->OnSerialize();
			//


			//
			char* buffer = (char*)malloc(sizeof(T) + 1);
			memcpy(buffer, t, sizeof(T));
			buffer[sizeof(T)] = '\0';
			SAVE_STREAM << buffer << std::endl;
			SAVE_STREAM << std::endl;
			//
		};

		template <typename T>
		static void Pull(T* t)
		{

		};
	};
};

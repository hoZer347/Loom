#include "Macro Helpers.h"


export module Serialize;

import <iostream>;

import <mutex>;
import <string>;
import <fstream>;
import <functional>;
import <unordered_map>;


namespace Loom
{
	HAS_FUNCTION_DECL(begin);
	HAS_FUNCTION_DECL(end);

	HAS_FUNCTION_DECL(OnSerialize);
	HAS_FUNCTION_DECL(OnDeserialize);



	inline void SubSerialize(std::ofstream& stream)
	{ };

	template <typename ARG, typename... ARGS>
	inline void SubSerialize(std::ofstream& stream, ARG* arg, ARGS*... args)
	{
		if constexpr (HAS_FUNCTION_TEST(ARG, OnSerialize))
			arg->OnSerialize();

		if constexpr (
			HAS_FUNCTION_TEST(ARG, begin) &&
			HAS_FUNCTION_TEST(ARG, end))
		{
			size_t size = arg->size();
			stream.write(reinterpret_cast<char*>(&size), sizeof(size));
			arg->reserve(size);
			stream.write(reinterpret_cast<char*>(arg->data()), sizeof(ARG::value_type) * size);
		}

		else stream.write(reinterpret_cast<char*>(arg), sizeof(ARG));

		SubSerialize(stream, args...);
	};

	export template <typename T, typename... ARGS>
	inline void Serialize(const char* path, T* t, ARGS*... args)
	{
		std::ofstream stream{ path, std::ios::binary };

		SubSerialize(stream, args...);

		stream.close();
	};



	inline void SubDeserialize(std::ifstream& stream)
	{ };

	template <typename ARG, typename... ARGS>
	inline void SubDeserialize(std::ifstream& stream, ARG* arg, ARGS*... args)
	{
		if constexpr (HAS_FUNCTION_TEST(ARG, OnDeserialize))
			arg->OnDeserialize();

		if constexpr (
			HAS_FUNCTION_TEST(ARG, begin) &&
			HAS_FUNCTION_TEST(ARG, end))
		{
			size_t size;
			stream.read(reinterpret_cast<char*>(&size), sizeof(size_t));
			arg->reserve(size);
			stream.read(reinterpret_cast<char*>(arg->data()), sizeof(ARG::value_type) * size);
		}

		else stream.read(reinterpret_cast<char*>(arg), sizeof(ARG));

		SubDeserialize(stream, args...);
	};

	export template <typename T, typename... ARGS>
	inline void Deserialize(const char* path, T* t, ARGS*... args)
	{
		std::ifstream stream{ path, std::ios::binary };

		SubDeserialize(stream, args...);

		stream.close();
	};
};

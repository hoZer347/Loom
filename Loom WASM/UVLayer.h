#pragma once

#include "glm/glm.hpp"

#include <bit>
#include <cstdint>
#include <tuple>
#include <vector>


namespace Loom
{
	// One of a mesh's raw UV layers: its id, the 32 bit words each vertex
	// holds of it, and those words, vertex after vertex.
	struct UVLayer
	{
		uint32_t id = 0;
		uint32_t words = 0;
		std::vector<uint32_t> values;

		bool operator==(const UVLayer&) const = default;
	};

	// How a T goes into a layer's words, the way a vertex stage's UV_<id>
	// declaration reads it back: a float, int or unsigned int as its bits, a
	// bool as 1 or 0, and a vector as its components in turn.
	template <typename T>
	struct UVWords;

	template <>
	struct UVWords<float>
	{
		static constexpr uint32_t count = 1;
		static void Pack(float value, uint32_t* out) { *out = std::bit_cast<uint32_t>(value); };
	};

	template <>
	struct UVWords<int>
	{
		static constexpr uint32_t count = 1;
		static void Pack(int value, uint32_t* out) { *out = std::bit_cast<uint32_t>(value); };
	};

	template <>
	struct UVWords<unsigned int>
	{
		static constexpr uint32_t count = 1;
		static void Pack(unsigned int value, uint32_t* out) { *out = value; };
	};

	template <>
	struct UVWords<bool>
	{
		static constexpr uint32_t count = 1;
		static void Pack(bool value, uint32_t* out) { *out = value ? 1u : 0u; };
	};

	template <glm::length_t L, typename T, glm::qualifier Q>
	struct UVWords<glm::vec<L, T, Q>>
	{
		static constexpr uint32_t count = L * UVWords<T>::count;

		static void Pack(const glm::vec<L, T, Q>& value, uint32_t* out)
		{
			for (glm::length_t i = 0; i < L; i++)
				UVWords<T>::Pack(value[i], out + i * UVWords<T>::count);
		};
	};

	template <typename... Ts>
	constexpr uint32_t UVWordsOf = (UVWords<Ts>::count + ...);

	template <typename... Ts>
	std::vector<uint32_t> PackUV(const std::vector<std::tuple<Ts...>>& values)
	{
		std::vector<uint32_t> words(values.size() * UVWordsOf<Ts...>);
		uint32_t* out = words.data();

		for (const std::tuple<Ts...>& value : values)
			std::apply(
				[&out](const Ts&... parts) { ((UVWords<Ts>::Pack(parts, out), out += UVWords<Ts>::count), ...); },
				value);

		return words;
	};

	template <typename T>
	std::vector<uint32_t> PackUV(const std::vector<T>& values)
	{
		std::vector<uint32_t> words(values.size() * UVWords<T>::count);

		for (size_t i = 0; i < values.size(); i++)
			UVWords<T>::Pack(values[i], words.data() + i * UVWords<T>::count);

		return words;
	};
};

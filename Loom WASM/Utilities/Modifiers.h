#pragma once

#include "Attributes.h"

#include <algorithm>
#include <cmath>
#include <string>


namespace Loom
{
	struct Md_AddInt final : TypedModifier<int>
	{
		Md_AddInt() = default;
		Md_AddInt(int value) : value(value) { };

		void Apply(int& v) const override { v += value; };

		int value = 0;
	};

	struct Md_MulInt final : TypedModifier<int>
	{
		Md_MulInt() = default;
		Md_MulInt(float multiplier) : multiplier(multiplier) { };

		void Apply(int& v) const override { v = int(v * multiplier); };

		float multiplier = 1.0f;
	};

	/// Multiplies, rounding rather than truncating, and never losing a whole point on the way.
	///
	/// Md_MulInt takes the floor, which is fine on a health pool and wrong on anything
	/// small: scaling an ATK of 2 by 1.41 leaves it at 2, so a rarity buys nothing at all
	/// on exactly the units with the least to start with. Anything that turns a ratio into
	/// a stat wants this one.
	struct Md_ScaleInt final : TypedModifier<int>
	{
		Md_ScaleInt() = default;
		Md_ScaleInt(float multiplier) : multiplier(multiplier) { };

		void Apply(int& v) const override
		{
			// std::round is away-from-zero at the midpoint, which is the rounding a
			// multiplicative modifier wants.
			const int scaled = int(std::round(v * double(multiplier)));

			// A multiplier above one that rounded straight back to where it started is a
			// bonus that did not happen. One point is the smallest a readout can honestly
			// show.
			v = multiplier > 1.0f && v > 0 && scaled <= v ? v + 1 : scaled;
		};

		float multiplier = 1.0f;
	};

	struct Md_ClampInt final : TypedModifier<int>
	{
		Md_ClampInt() = default;
		Md_ClampInt(int min, int max) : min(min), max(max) { };

		void Apply(int& v) const override { v = std::max(min, std::min(max, v)); };

		int min = 0;
		int max = 0;
	};

	struct Md_AddFloat final : TypedModifier<float>
	{
		Md_AddFloat() = default;
		Md_AddFloat(float value) : value(value) { };

		void Apply(float& v) const override { v += value; };

		float value = 0.0f;
	};

	struct Md_MulFloat final : TypedModifier<float>
	{
		Md_MulFloat() = default;
		Md_MulFloat(float multiplier) : multiplier(multiplier) { };

		void Apply(float& v) const override { v *= multiplier; };

		float multiplier = 1.0f;
	};

	struct Md_ClampFloat final : TypedModifier<float>
	{
		Md_ClampFloat() = default;
		Md_ClampFloat(float min, float max) : min(min), max(max) { };

		void Apply(float& v) const override { v = std::max(min, std::min(max, v)); };

		float min = 0.0f;
		float max = 0.0f;
	};

	struct Md_SetString final : TypedModifier<std::string>
	{
		Md_SetString() = default;
		Md_SetString(std::string value) : value(std::move(value)) { };

		void Apply(std::string& v) const override { v = value; };

		std::string value{ };
	};

	struct Md_AppendString final : TypedModifier<std::string>
	{
		Md_AppendString() = default;
		Md_AppendString(std::string value) : value(std::move(value)) { };

		void Apply(std::string& v) const override { v += value; };

		std::string value{ };
	};

	struct Md_Damage final : TypedModifier<int>
	{
		Md_Damage() = default;
		Md_Damage(int damage) : damage(damage) { };

		void Apply(int& v) const override { v -= damage; };

		int damage = 0;
	};

	struct Md_Heal final : TypedModifier<int>
	{
		Md_Heal() = default;
		Md_Heal(int healing) : healing(healing) { };

		void Apply(int& v) const override { v += healing; };

		int healing = 0;
	};
};

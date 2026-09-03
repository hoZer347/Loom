#pragma once

#include "Attributes.h"

#include <functional>
#include <iostream>
#include <string>


namespace Loom
{
	/// Something wired up elsewhere that runs when the attribute is invalidated, held as
	/// a std::function handed in by code.
	struct Tr_Callback final : AttributeTrigger
	{
		Tr_Callback() = default;
		Tr_Callback(std::function<void()> onInvalidate) : onInvalidate(std::move(onInvalidate)) { };

		void Execute() override
		{
			if (onInvalidate)
				onInvalidate();
		};

		std::function<void()> onInvalidate{ };
	};

	/// Invalidates another attribute -- how a derived stat is kept in step with the one it
	/// is computed from.
	struct Tr_Invalidate final : AttributeTrigger
	{
		Tr_Invalidate() = default;
		Tr_Invalidate(AttributeBase* target) : target(target) { };

		void Execute() override
		{
			if (target)
				target->Invalidate();
		};

		AttributeBase* target = nullptr;
	};

	struct Tr_Log final : AttributeTrigger
	{
		Tr_Log() = default;
		Tr_Log(std::string label) : label(std::move(label)) { };

		void Execute() override
		{
			std::cout << "[Attribute] " << label << " invalidated" << std::endl;
		};

		std::string label{ };
	};
};

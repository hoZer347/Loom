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
		Tr_Callback(std::function<void()> onInvalidate) : m_onInvalidate(std::move(onInvalidate)) { };

		void Execute() override
		{
			if (m_onInvalidate)
				m_onInvalidate();
		};

	private:
		std::function<void()> m_onInvalidate{ };
	};

	/// Invalidates another attribute -- how a derived stat is kept in step with the one it
	/// is computed from.
	struct Tr_Invalidate final : AttributeTrigger
	{
		Tr_Invalidate() = default;
		Tr_Invalidate(AttributeBase* target) : m_target(target) { };

		void Execute() override
		{
			if (m_target)
				m_target->Invalidate();
		};

	private:
		AttributeBase* m_target = nullptr;
	};

	struct Tr_Log final : AttributeTrigger
	{
		Tr_Log() = default;
		Tr_Log(std::string label) : m_label(std::move(label)) { };

		void Execute() override
		{
			std::cout << "[Attribute] " << m_label << " invalidated" << std::endl;
		};

	private:
		std::string m_label{ };
	};
};

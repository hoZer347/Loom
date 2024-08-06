#pragma once

#include "GameObject.h"


namespace Loom
{
	struct Engine final
	{
		void Init();

	protected:
		template <typename S, typename... T>
		friend inline constexpr void Add(
			const ptr<S>& _ptr,
			const ptr<T>&... _ptrs) noexcept;

	private:
		static void UpdateAll();
		static void RenderAll();

		Engine() = delete;
		~Engine() = delete;
	};
};

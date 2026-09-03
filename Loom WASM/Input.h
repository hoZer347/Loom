#pragma once

#include "Loom API.h"


namespace Loom
{
	struct LOOM_API Input
	{
		static bool GetMouseButtonDown(int button)
		{
			return false;
		};

		static bool GetMouseButton(int button)
		{
			return false;
		};

		static bool GetMouseButtonUp(int button)
		{
			return false;
		};

		static inline int screen_width = 0;
		static inline int screen_height = 0;

		static inline int mouse_x = 0;
		static inline int mouse_y = 0;

	protected:
		friend struct Engine;

		Input() = delete;
		~Input() = delete;

		static void Init();
	};
};

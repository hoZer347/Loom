#pragma once


namespace Loom
{
	struct Input
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

	protected:
		friend struct Engine;

		Input() = delete;
		~Input() = delete;

		static void Init();
	};
};

export module Geometry;

import Component;


namespace Loom
{
	export struct Rect final :
		public Component
	{
		Rect(
			const unsigned int& x,
			const unsigned int& y,
			const unsigned int& w,
			const unsigned int& h);

		virtual ~Rect();

		void Update() override;

	private:
		unsigned int* position;
		unsigned int* dimensions;
	};
};

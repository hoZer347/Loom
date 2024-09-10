export module Geometry;

import Component;

import <glm/glm.hpp>;
using namespace glm;


namespace Loom::Geometry
{
	export struct Rect final :
		public Component<Rect>
	{
		Rect(
			const float& x,
			const float& y,
			const float& w,
			const float& h,
			const vec4& color);

		~Rect();

		void OnGui() override;
		void OnRender() override;

		vec4 color;
		float x, y, w, h;
	};

	export struct Cube
	{

	};
};

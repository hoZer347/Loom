export module Transform;

import DataTypes;
import <vector>;

import <glm/glm.hpp>;
using namespace glm;


namespace Loom
{
	export struct Transform final
	{
		Transform()
		{ };

		inline constexpr operator mat4& const () { return m_transform; }

	private:
		mat4 m_transform = mat4(1);
	};
};

export module Mesh;

import Component;

import <GL/glew.h>;


namespace Loom
{
	export struct Mesh final :
		public Component<Mesh>
	{
		Mesh();
		~Mesh();

		void OnGui() override;
		void OnRender() override;

		GLuint vtxs_id = 0;
		GLuint inds_id = 0;
	};
};

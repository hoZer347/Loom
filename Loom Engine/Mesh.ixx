export module Mesh;

import Component;
import "imgui.h";

import <gl/glew.h>;
import <GLFW/glfw3.h>;

import <glm/glm.hpp>;
using namespace glm;

#define MAX_FILE_NAME_SIZE 256


namespace Loom
{
	export struct Mesh : Component<Mesh>
	{
		void OnGui() override
		{
			ImGui::Text("Mesh");
		};

		void OnRender() override
		{
			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glDisableVertexAttribArray(2);
			glDisableVertexAttribArray(3);

			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), 0);													// Position
			glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(vec4), (void*)sizeof(vec3));									// Color
			glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)(sizeof(vec3) + sizeof(vec4)));				// Normal
			glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)(sizeof(vec3) + sizeof(vec4) + sizeof(vec3)));	// UV
			
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
			glEnableVertexAttribArray(2);
			glEnableVertexAttribArray(3);
		};
	};
};

export module Mesh;

import Component;
import "imgui.h";
import <assimp/Importer.hpp>;

import <gl/glew.h>;
import <GLFW/glfw3.h>;

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

		};

		Assimp::Importer importer;
	};
};

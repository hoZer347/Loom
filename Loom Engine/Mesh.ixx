#include <assimp/Importer.hpp>;

export module Mesh;

import Component;
import "imgui.h";

import <vector>;

import <string>;
import <glm/glm.hpp>;
using namespace glm;


#define MAX_FILE_NAME_SIZE 256

namespace Loom
{
	export struct Mesh : Component<Mesh>
	{
		Mesh()
		{
			file_name[0] = '\0';
		};

		void OnGui()
		{
			ImGui::InputText("File Name", file_name, MAX_FILE_NAME_SIZE);
		};

		char file_name[256];

		Assimp::Importer importer{ };
	};
};

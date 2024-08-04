#include "UI.h"

#include "imgui.h"
#include "glm/glm.hpp"

using namespace glm;


int main()
{
	auto ui0 = []()
	{
		static ImGuiIO& io = ImGui::GetIO(); (void)io;
		static float f = 0.0f;
		static int counter = 0;
		static vec3 clear_color = vec3(0.3, 0, 0);

		ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

		ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		
		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
			counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::End();
	};

	Loom::AddUI(ui0);

	Loom::StartImguiDemo();

	return 0;
};

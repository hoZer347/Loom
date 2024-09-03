import MainMenu;

#include "imgui.h"
#include "imfilebrowser.h"

#include <string>
#include <list>

ImGui::FileBrowser fileDialog;


namespace Loom
{
	void MainMenu::Gui()
	{
		static std::list<float> deltaTimes{ };

		if (ImGui::Begin("Menu"))
		{
			ImGui::Text("FPS: ");
			ImGui::SameLine();

			// Showing FPS
			float total = 0;
			deltaTimes.emplace_back(ImGui::GetIO().DeltaTime);
			if (deltaTimes.size() > 100)
				deltaTimes.pop_front();
			for (auto& i : deltaTimes)
				total += i;
			//

			ImGui::Text(std::to_string(1 / (total / deltaTimes.size())).c_str());
		};

		if (ImGui::Button("Open File Dialog")) 
			fileDialog.Open();

		// Show the file dialog
		fileDialog.Display();

		if (fileDialog.HasSelected())
		{
			std::string filePath = fileDialog.GetSelected().string();
			ImGui::Text("Selected file: %s", filePath.c_str());
			fileDialog.ClearSelected();
		};

		ImGui::End();
	};
};

import Server;
import Networking;

#include "imgui.h"

import <thread>;


namespace Loom
{
	TCP_Server::~TCP_Server()
	{
		StopTCPServer();

		if (thread.joinable())
			thread.join();
	};

	void TCP_Server::OnAttach()
	{
		
	};

	void TCP_Server::OnUpdate()
	{

	};

	void TCP_Server::OnGui()
	{
		ImGui::PushID((void*)this);

		if (ImGui::Checkbox("Running", &running))
		{
			if (running) thread = std::thread(OpenTCPServerOnThisThread);
			else
			{
				StopTCPServer();

				if (thread.joinable())
					thread.join();
			};
		};

		if (!running)
			ImGui::InputScalar("Port", ImGuiDataType_U64, &port);
		else ImGui::Text(std::string("Port: " + std::to_string(port)).c_str());

		ImGui::PopID();
	};
	
	void TCP_Server::OnDetach()
	{
		StopTCPServer();

		if (thread.joinable())
			thread.join();
	};
};

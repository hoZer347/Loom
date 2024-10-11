#include "Macro Helpers.h"

export module Server;

import Component;
import Networking;
import <mutex>;
import <queue>;
import <functional>;


namespace Loom
{
	struct ServerBase
	{
		uint64_t port = 1111;
		bool running = std::function<bool()>([]() { return false; })();
	};

	export struct TCP_Server final :
		public Component<TCP_Server>,
		public ServerBase
	{
		virtual ~TCP_Server();

		void OnAttach() override;
		void OnUpdate() override;
		void OnGui() override;
		void OnDetach() override;

		std::thread thread;
	};

	export struct UDP_Server final :
		public Component<UDP_Server>,
		public ServerBase
	{
		void OnAttach() override;
		void OnUpdate() override;
		void OnDetach() override;

		bool running = false;
	};

	export struct Network :
		public Component<Network>
	{
		void OnAttach() override;
		void OnUpdate() override;
		void OnGui() override;
		void OnDetach() override;

		std::queue<Message*> messages;
		std::mutex mutex;
	};
};

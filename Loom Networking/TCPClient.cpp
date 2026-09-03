#include "TCPClient.h"


namespace Loom
{
	void TCPClient::OnAttach()
	{
		std::thread([]()
			{
				static const std::string msg = "Test";
				static const std::string prepend =
					"GET / HTTP / 1.1\r\n"
					"Host: dev.loomhozer.ca\r\n"
					"Content - Length : " + std::to_string(msg.length()) + "\r\n\r\n";

				io_context.restart();

				auto endpoints = resolver.resolve("dev.loomhozer.ca", "8000");

				boost::asio::connect(_socket, endpoints);

				std::thread([]()
					{
						try
						{
							while (_socket.is_open())
								io_context.run();
						}
						catch (const std::exception& e)
						{
							std::cout << "io.run error: " << e.what() << "\n";
						}
					}).detach();

			}).detach();
	};
};

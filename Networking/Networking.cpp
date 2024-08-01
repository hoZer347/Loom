#include "Networking.h"

#include <iostream>


namespace Loom
{
	Client::Client() : NetworkObject()
	{
		try
		{
			boost::asio::io_service io_service{ };

			udp::resolver resolver(io_service);
			udp::resolver::query query(udp::v4(), "70.54.28.7", "1111");
			udp::resolver::iterator iterator = resolver.resolve(query);

			udp::socket socket{ io_service };
			socket.open(udp::v4());

			udp::endpoint receiver_endpoint = *iterator;

			shouldStop = false;

			sendBuffer.resize(1024);
			recvBuffer.resize(1024);

			while (!shouldStop)
			{
				std::string s;
				std::cin >> s;
				socket.send_to(boost::asio::buffer(s), receiver_endpoint);
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};
	};

	Server::Server() : NetworkObject()
	{
		try
		{
			boost::asio::io_service io_service{ };
			udp::endpoint endpoint{ udp::v4(), 1111 };
			udp::socket socket{ io_service, endpoint };

			shouldStop = false;

			sendBuffer.resize(1024);
			recvBuffer.resize(1024);

			while (!shouldStop)
			{
				size_t size = socket.receive(boost::asio::buffer(recvBuffer));

				std::cout << recvBuffer.data() << std::endl;
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};
	};
};

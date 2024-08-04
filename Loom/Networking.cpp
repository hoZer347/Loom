#include "Networking.h"

#include <unordered_set>
#include <iostream>


namespace Loom
{
	static inline bool shouldStop;
	static inline thread_local bool is_thread_occupied = false;

	std::unordered_set<udp::endpoint> endpoints{ };

	void StartServer()
	{
		if (is_thread_occupied)
		{
			std::cerr << "Thread is already occupied" << std::endl;
			return;
		};

		is_thread_occupied = true;

		try
		{
			boost::asio::io_service io_service{ };
			udp::endpoint endpoint{ udp::v4(), 1111 };
			udp::socket socket{ io_service, endpoint };

			shouldStop = false;

			std::vector<char> recvBuffer;
			std::vector<char> sendBuffer;

			sendBuffer.resize(1024);
			recvBuffer.resize(1024);

			std::cout << "----- Server Started -----" << std::endl;

			while (!shouldStop)
			{
				size_t size = socket.receive_from(
					boost::asio::buffer(recvBuffer),
					endpoint);

				if (size > 0)
				{
					size = socket.send_to(
						boost::asio::buffer(recvBuffer),
						endpoint);
				};

				std::cout << "Message Type: " << *((long long*)recvBuffer.data()) << std::endl;

				if (*((long long*)recvBuffer.data()) == 1u)
				{
					std::cout << "Test" << std::endl;
				}
				else std::cout << "Message Received on Server: " << recvBuffer.data() << std::endl;

				for (auto& i : recvBuffer)
					i = 0;
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};

		is_thread_occupied = false;
	};

	void StartClient()
	{
		if (is_thread_occupied)
		{
			std::cerr << "Thread is already occupied" << std::endl;
			return;
		};

		is_thread_occupied = true;

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

			std::vector<char> recvBuffer;
			std::vector<char> sendBuffer;

			sendBuffer.resize(1024);
			recvBuffer.resize(1024);

			std::cout << "----- Client Started -----" << std::endl;

			while (!shouldStop)
			{
				std::string s;
				std::cin >> s;
				socket.send_to(
					boost::asio::buffer("\0\0\0\0"),
					receiver_endpoint);

				size_t size = socket.receive_from(
					boost::asio::buffer(recvBuffer),
					receiver_endpoint);

				if (size > 0)
					std::cout << "Received Message: " << recvBuffer.data() << std::endl;
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};

		is_thread_occupied = false;
	};
};

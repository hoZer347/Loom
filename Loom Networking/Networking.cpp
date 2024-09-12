import Networking;

#include "Definitions.hpp"
#include <string>
#include <queue>
#include <vector>
#include <iostream>
#include <unordered_set>
#include <unordered_map>

#include <boost/asio.hpp>

using boost::asio::ip::udp;

#define MIN_BUFFER_SIZE 4


namespace Loom
{
	static bool shouldStop;

	static inline std::queue<Message*> outgoing{ };
	static inline std::mutex outgoing_mut{ };
	static inline std::unordered_set<udp::endpoint> endpoints{ };

	static inline std::unordered_map<MessageType, void(*)(Message*)> handles{ };

	void OpenServerOnThisThread()
	{
		try
		{
			boost::asio::io_service io_service{ };
			udp::endpoint endpoint{ udp::v4(), 1111 };
			udp::socket socket{ io_service, endpoint };

			BYTE recvBuffer[1024]{ };
			BYTE sendBuffer[1024]{ };

			std::cout << "----- Server Started -----" << std::endl;

			while (!shouldStop)
			{

				if (size_t size = socket.receive_from(
					boost::asio::buffer(recvBuffer),
					endpoint))
				{
					MessageType message_type = ((Message*)recvBuffer)->message_type;

					if (message_type == LOOM_NULL_REQUEST) std::cout << "NULL-Type message was sent" << std::endl;
					else if (message_type == LOOM_TEXT_MESSAGE) std::cout << "Received Message: " << ((TextMessage*)recvBuffer)->text << std::endl;
					else if (handles.contains(message_type)) handles[message_type]((Message*)&recvBuffer);
					else std::cerr << "No handle for message type: " << message_type << std::endl;
				};
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};
	};

	void OpenClientOnThisThread()
	{
		try
		{
			boost::asio::io_service io_service{ };

			udp::resolver resolver(io_service);
			udp::resolver::query query(udp::v4(), "74.14.200.14", "1111");
			udp::resolver::iterator iterator = resolver.resolve(query);

			udp::socket socket{ io_service };
			socket.open(udp::v4());

			udp::endpoint endpoint = *iterator;

			shouldStop = false;

			BYTE recvBuffer[1024]{ };
			BYTE sendBuffer[1024]{ };

			std::cout << "----- Client Started -----" << std::endl;

			std::thread t([]()
				{
					while (true)
					{
						std::string s;
						std::getline(std::cin, s);
						auto text = new TextMessage();
						text->message_type = LOOM_TEXT_MESSAGE;
						int i = 0;
						for (; i < s.size(); i++)
							text->text[i] = s[i];
						text->text[i] = '\0';
						Send<LOOM_TEXT_MESSAGE>(text);
					};
				});
			t.detach();

			while (!shouldStop)
			{
				{
					std::lock_guard<std::mutex> lock{ outgoing_mut };

					while (!outgoing.empty())
					{
						memcpy(
							recvBuffer,
							outgoing.front(),
							outgoing.front()->size);

						if (size_t size = socket.send_to(
							boost::asio::buffer(recvBuffer),
							endpoint))
						{

						};

						outgoing.pop();
					};
				};
			};
		}
		catch (const std::exception& ex)
		{
			std::cerr << ex.what() << std::endl;
			shouldStop = true;
		};
	};

	void Send(MessageType message_type, Message* message, size_t size)
	{
		message->message_type = message_type;
		message->size = size;

		std::lock_guard<std::mutex> lock{ outgoing_mut };

		outgoing.emplace(message);
	};

	//void Request(RequestType request_type, Message* sent, size_t sent_size, Message* receive, size_t receive_size)
	//{

	//};

	void SetHandle(MessageType message_type, void(*message_interpretter)(Message*))
	{
		handles[message_type] = message_interpretter;
	};
};

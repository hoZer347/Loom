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
	static inline bool tcp_server_on = false;
	static inline bool udp_server_on = false;

	static inline bool tcp_client_on = false;
	static inline bool udp_client_on = false;

	static inline std::queue<Message*> outgoing{ };
	static inline std::mutex outgoing_mut{ };
	static inline std::unordered_set<udp::endpoint> endpoints{ };

	static inline std::unordered_map<MessageType, void(*)(Message*)> handles{ };

	void OpenUDPServerOnThisThread()
	{
		//try
		//{
		//	boost::asio::io_service io_service{ };
		//	udp::endpoint endpoint{ udp::v4(), 1111 };
		//	udp::socket socket{ io_service, endpoint };

		//	BYTE recvBuffer[1024]{ };
		//	BYTE sendBuffer[1024]{ };

		//	std::cout << "----- Server Started -----" << std::endl;

		//	udp_server_on = true;

		//	while (udp_server_on)
		//	{

		//		if (size_t size = socket.receive_from(
		//			boost::asio::buffer(recvBuffer),
		//			endpoint))
		//		{
		//			MessageType message_type = ((Message*)recvBuffer)->message_type;

		//			if (message_type == LOOM_NULL_REQUEST) std::cout << "NULL-Type message was sent" << std::endl;
		//			else if (message_type == LOOM_TEXT_MESSAGE) std::cout << "Received Message: " << ((TextMessage*)recvBuffer)->text << std::endl;
		//			else if (handles.contains(message_type)) handles[message_type]((Message*)&recvBuffer);
		//			else std::cerr << "No handle for message type: " << message_type << std::endl;
		//		};
		//	};
		//}
		//catch (const std::exception& ex)
		//{
		//	std::cerr << ex.what() << std::endl;
		//};

		//udp_server_on = false;
	};

	void OpenUDPClientOnThisThread()
	{
		//try
		//{
		//	boost::asio::io_service io_service{ };

		//	udp::resolver resolver(io_service);
		//	udp::resolver::query query(udp::v4(), "74.14.200.14", "1111");
		//	udp::resolver::iterator iterator = resolver.resolve(query);

		//	udp::socket socket{ io_service };
		//	socket.open(udp::v4());

		//	udp::endpoint endpoint = *iterator;

		//	BYTE recvBuffer[1024]{ };
		//	BYTE sendBuffer[1024]{ };

		//	std::cout << "----- Client Started -----" << std::endl;

		//	udp_client_on = true;

		//	std::thread t([]()
		//		{
		//			while (udp_client_on)
		//			{
		//				std::string s;
		//				std::getline(std::cin, s);
		//				auto text = new TextMessage();
		//				text->message_type = LOOM_TEXT_MESSAGE;
		//				int i = 0;
		//				for (; i < s.size(); i++)
		//					text->text[i] = s[i];
		//				text->text[i] = '\0';
		//				Send<LOOM_TEXT_MESSAGE>(text);
		//			};
		//		});
		//	t.detach();

		//	while (udp_client_on)
		//	{
		//		std::lock_guard<std::mutex> lock{ outgoing_mut };

		//		while (!outgoing.empty())
		//		{
		//			memcpy(
		//				recvBuffer,
		//				outgoing.front(),
		//				outgoing.front()->size);

		//			if (size_t size = socket.send_to(
		//				boost::asio::buffer(recvBuffer),
		//				endpoint))
		//			{

		//			};

		//			outgoing.pop();
		//		};	
		//	};
		//}
		//catch (const std::exception& ex)
		//{
		//	std::cerr << ex.what() << std::endl;
		//};

		//udp_client_on = false;
	};

	static inline std::string make_http_response()
	{
		std::string response = "HTTP/1.1 200 OK\r\n";
		//response += "Content-Type: text/plain\r\n";
		//response += "Content-Length: 13\r\n";
		//response += "\r\n";
		//response += "Hello, World!";
		std::string html;
		html += "<!DOCTYPE html>\n";
		html += "<html>\n";
		html += "<body>\n";
		html += "<h1>My First Heading</h1>\n";
		html += "<p>My first paragraph.</p>\n";
		html += "</body>\n";
		html += "</html>\r\n\0";

		std::cout << "Sending: " << html << std::endl; 

		response += "Content-Type: text/html\r\n";
		response += "Content-Length: " + std::to_string(html.length() + 1) + "\r\n";
		response += html;

		return response;
	};

	void OpenTCPServerOnThisThread()
	{
		using boost::asio::ip::tcp;

		try
		{
			boost::asio::io_service io_service;

			// Log the startup information
			std::cout << "Starting server on port 1111..." << std::endl;

			// Create an acceptor to listen for incoming connections
			tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), 1111));

			tcp_server_on = true;

			while (tcp_server_on)
			{
				try
				{
					// Create a socket for the incoming connection
					tcp::socket socket(io_service);

					// Log that we are waiting for a connection
					std::cout << "Waiting for a connection..." << std::endl;

					// Wait and accept an incoming connection
					acceptor.accept(socket);

					// Log the client's IP address
					std::cout << "Accepted connection from: "
						<< socket.remote_endpoint().address().to_string() << std::endl;

					// Generate the HTTP response
					std::string response = make_http_response();

					// Write the response to the socket
					boost::asio::write(socket, boost::asio::buffer(response));

					// Log that the response was sent
					std::cout << "Sent response to client." << std::endl;

					// Close the socket (it automatically closes after sending the response)
					socket.shutdown(tcp::socket::shutdown_both);
					socket.close();

					// Log that the connection is closed
					std::cout << "Connection closed." << std::endl;
				}
				catch (std::exception& e)
				{
					// Log if any error occurs in the connection handling
					std::cerr << "Error handling connection: " << e.what() << std::endl;
				};
			};
		}
		catch (std::exception& e)
		{
			std::cerr << "Error: " << e.what() << std::endl;
		};

		tcp_server_on = false;
	};

	void OpenTCPClientOnThisThread()
	{
		
	};

	void StopUDPServer()
	{
		udp_server_on = false;

		std::lock_guard lock{ outgoing_mut };
	};

	void StopUDPClient()
	{
		udp_client_on = false;

		std::lock_guard lock{ outgoing_mut };
	};

	void StopTCPServer()
	{
		tcp_server_on = false;

		std::lock_guard lock{ outgoing_mut };
	};

	void StopTCPClient()
	{
		tcp_client_on = false;

		std::lock_guard lock{ outgoing_mut };
	};

	const bool& GetIsUDPServerOn()
	{
		return udp_server_on;
	};

	const bool& GetIsUDPClientOn()
	{
		return udp_client_on;
	};

	const bool& GetIsTCPServerOn()
	{
		return tcp_server_on;
	};

	const bool& GetIsTCPClientOn()
	{
		return tcp_client_on;
	};

	void Send(MessageType message_type, Message* message, size_t size)
	{
		message->message_type = message_type;
		message->size = size;

		std::lock_guard<std::mutex> lock{ outgoing_mut };

		outgoing.emplace(message);
	};

	void SetHandle(MessageType message_type, void(*message_interpretter)(Message*))
	{
		handles[message_type] = message_interpretter;
	};
};

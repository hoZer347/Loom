import Networking;

#include "Definitions.hpp"
#include <fstream>
#include <string>
#include <queue>
#include <vector>
#include <iostream>
#include <unordered_set>
#include <unordered_map>

#include <boost/asio.hpp>

using boost::asio::ip::udp;

#define MIN_BUFFER_SIZE 4
#define TCP_HOST_IP_PORT 80


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

	void OpenUDPServerOnThisThread(std::string project_directory)
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

	void OpenUDPClientOnThisThread(std::string project_directory)
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
	
	void OpenTCPServerOnThisThread(std::string project_directory)
	{
		using boost::asio::ip::tcp;

		try
		{
			boost::asio::io_service io_service{ };

			std::cout << "Starting server on port " + std::to_string(TCP_HOST_IP_PORT) + "..." << std::endl;

			tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), TCP_HOST_IP_PORT));
			tcp_server_on = true;

			while (tcp_server_on)
			{
				try
				{
					tcp::socket socket(io_service);

					std::cout << "Waiting for a connection..." << std::endl;
					acceptor.accept(socket);

					std::cout << "Accepted connection from: "
						<< socket.remote_endpoint().address().to_string() << std::endl;

					// Read request from the client
					boost::asio::streambuf request;
					boost::asio::read_until(socket, request, "\r\n");
					std::istream request_stream(&request);

					std::string method, path, version;
					request_stream >> method >> path >> version;
					std::cout << "Request path: " << path << std::endl;

					// Map request to file type (adjust paths if necessary)
					std::string file_path = project_directory + (path == "/" ? "/index.html" : path);

					//
					std::string toFind = "%20";
					std::string toReplace = " ";
					size_t pos = file_path.find(toFind);

					while (pos != std::string::npos)
					{
						file_path.replace(pos, toFind.length(), toReplace);
						pos = file_path.find(toFind, pos + toReplace.length());
					};
					//

					std::ifstream file(file_path, std::ios::binary);
					if (!file.is_open())
					{
						std::cerr << "Failed to open the file: " << file_path << std::endl;
						std::string not_found_response = "HTTP/1.1 404 Not Found\r\n\r\nFile not found.";
						boost::asio::write(socket, boost::asio::buffer(not_found_response));
					}
					else
					{
						std::stringstream buffer;
						buffer << file.rdbuf();
						std::string content = buffer.str();

						// Set Content-Type based on the requested file
						std::string content_type;
						if (file_path.ends_with(".html"))
							content_type = "text/html";
						else if (file_path.ends_with(".js"))
							content_type = "application/javascript";
						else if (file_path.ends_with(".wasm"))
							content_type = "application/wasm";
						else if (file_path.ends_with(".data.gz") || file_path.ends_with(".framework.js.gz"))
							content_type = "application/octet-stream";
						else
							content_type = "text/plain";

						// Send the HTTP response
						std::string response = "HTTP/1.1 200 OK\r\n";
						response += "Content-Type: " + content_type + "\r\n";
						response += "Content-Length: " + std::to_string(content.size()) + "\r\n";
						// Add this line for gzip files
						if (file_path.ends_with(".gz")) response += "Content-Encoding: gzip\r\n";
						response += "\r\n"; // Blank line between headers and body
						response += content;

						boost::asio::write(socket, boost::asio::buffer(response));
						std::cout << "Sent response for " << path << std::endl;
					};

					// Close the socket
					socket.shutdown(tcp::socket::shutdown_both);
					socket.close();
				}
				catch (std::exception& e)
				{
					std::cerr << "Error handling connection: " << e.what() << std::endl;
				};
			};
		}
		catch (std::exception& e)
		{
			std::cerr << "Error: " << e.what() << std::endl;
		};

		tcp_server_on = false;
	}

	void OpenTCPClientOnThisThread(std::string project_directory)
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

#include "TCPServer.h"


namespace Loom
{
	void TCPServer::OnAttach()
	{
		acceptor.listen();

		std::thread([this]()
			{
				while (true)
				{
					auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context);
					acceptor.accept(*socket);
					std::cout << std::endl << "New connection accepted! (" << socket->remote_endpoint() << ")" << std::endl;

					std::thread([socket]() mutable
						{
							try
							{
								while (socket->is_open())
								{
									char data[1024];
									boost::system::error_code error;
									size_t length = socket->read_some(boost::asio::buffer(data), error);
									if (error == boost::asio::error::eof)
										break; // Connection closed cleanly by peer.
									else if (error)
										throw boost::system::system_error(error); // Some other error.
									std::cout << "Received data: " << std::string(data, length) << std::endl;

									const std::string msg = "Hello World";
									const std::string response =
										"HTTP/1.1 200 OK\r\n"
										"Content-Type: text/plain\r\n"
										"Content-Length: " + std::to_string(msg.length()) + "\r\n\r\n"
										+ msg;

									std::cout << "Sending: " << response << std::endl;

									boost::asio::write(*socket, boost::asio::buffer(response));
								};
							}
							catch (std::exception& e)
							{
								std::cerr << "Exception in connection thread: " << e.what() << std::endl;
							};
						}).detach();
				};
			}).detach();
	};

	void TCPServer::OnUpdate()
	{
		
	};
};

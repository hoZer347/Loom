#pragma once

#include "Client.h"
#include "DataPackage.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <iostream>
#include <string>


namespace Loom
{
	struct TCPClient : public Client, public Component<TCPClient>
	{
		template <typename _Send>
		static inline void Send(const _Send* package)
		{
			std::thread([package]()
				{
					static const std::string msg = "Test";
					static const std::string prepend =
						"GET / HTTP / 1.1\r\n"
						"Host: dev.loomhozer.ca\r\n"
						"Content - Length : " + std::to_string(msg.length()-1) + "\r\n\r\n"
						+ msg;

					while (!_socket.is_open())
						std::this_thread::sleep_for(std::chrono::milliseconds(100));

					boost::asio::write(_socket, boost::asio::buffer(prepend));

				}).detach();
		};

		void OnAttach() override;

	protected:
		TCPClient() { };
		friend struct GameObject;

	private:

		static inline boost::asio::io_context io_context { };
		static inline boost::asio::ip::tcp::socket _socket{ io_context };
		static inline boost::asio::ip::tcp::resolver resolver{ io_context };
	};
};

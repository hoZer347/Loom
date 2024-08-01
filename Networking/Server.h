#pragma once

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include "Templates.h"

using boost::asio::ip::udp;


namespace Loom
{
	class Server
	{
		udp::socket _socket;
		udp::endpoint _remoteEndpoint;
		std::array<char, 1024> _recvBuffer;

	public:
		Server(boost::asio::io_service& io_service)
			: _socket(io_service, udp::endpoint(udp::v4(), 1111))
		{
			startReceive();
		};

	private:
		void startReceive()
		{
			_socket.async_receive_from(
				boost::asio::buffer(_recvBuffer), _remoteEndpoint,
				boost::bind(&Server::handleReceive, this,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred));
		};

		void handleReceive(
			const boost::system::error_code& error,
			std::size_t bytes_transferred)
		{
			if (!error || error == boost::asio::error::message_size)
			{
				std::string receivedMessage(_recvBuffer.data(), bytes_transferred);
				std::cout << "Received message: " << receivedMessage << std::endl;

				auto message = std::make_shared<std::string>("Hello, World\n");

				_socket.async_send_to(boost::asio::buffer(*message), _remoteEndpoint,
					boost::bind(&Server::handleSend, this, message,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
			};
		};

		void handleSend(
			std::shared_ptr<std::string> message,
			const boost::system::error_code& /*error*/,
			std::size_t /*bytes_transferred*/)
		{
			startReceive();
		};
	};

	template <Str NAME, typename Sent, typename Received>
	int Receive(Sent& sent, Received& received)
	{
		static inline boost::asio::io_service io_service{ };
		static inline udp::socket socket{ io_service, udp::endpoint(udp::v4(), 1111) };

		return 0;
	};
};

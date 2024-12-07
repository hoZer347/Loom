#pragma once

#define BOOST_ASIO_DISABLE_MODULE
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <string>
#include <queue>

#ifndef TCP_HOST_IP_PORT
#define TCP_HOST_IP_PORT 80
#endif

// This file is here because of boost not working with C++ 20+ modules


namespace Loom
{
	struct Server
	{
		void WaitUntilDone();

		virtual ~Server();

	protected:
		virtual void Update() = 0;

		Server(const std::string& project_directory);

		boost::asio::io_context io_context{ };

		const std::string project_directory;

	private:
		std::recursive_mutex mutex{ };
		bool isRunning = false;
		std::thread thread;
		SSL_CTX* ctx;
	};

	struct TCPServer final : Server
	{
		TCPServer(const std::string& project_directory) :
			Server(project_directory)
		{ };

	protected:
		void Update();

	private:
		boost::asio::ip::tcp::acceptor acceptor
		{
			io_context,
			boost::asio::ip::tcp::endpoint
			{
				boost::asio::ip::tcp::v4(),
				TCP_HOST_IP_PORT
			}
		};
	};

	struct UDPServer final : Server
	{
		UDPServer(const std::string& project_directory) :
			Server(project_directory)
		{ };

	protected:
		void Update();
	};
};

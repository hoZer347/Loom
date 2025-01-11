#pragma once

#include "Utils.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <iostream>
#include <string>
#include <queue>
#include <mutex>

#ifndef TCP_HOST_IP_PORT
#define TCP_HOST_IP_PORT 80
#endif


namespace Loom
{
	struct Server : public RAIIPersistentNetworkObject
	{
		void ChangeProjectDirectory(const std::string& project_directory)
		{
			std::lock_guard lock{ mutex };
			this->project_directory = project_directory;
		};

	protected:
		Server(const std::string& project_directory) :
			project_directory(project_directory)
		{ };

		std::string project_directory;

	private:
		std::recursive_mutex mutex;
	};

	struct TCPServer final : public Server
	{
		TCPServer(const std::string& project_directory);

		void Update();

	private:
		boost::asio::ip::tcp::acceptor acceptor;
	};

	struct UDPServer final : public Server
	{
		UDPServer(const std::string& project_directory) :
			Server(project_directory)
		{ };

		void Update();
	};
};

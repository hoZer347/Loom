#pragma once

//#define _WIN32_WINNT

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include <vector>

using boost::asio::ip::udp;

#define MIN_BUFFER_SIZE 4


namespace Loom
{
	typedef size_t RequestType;
	
	void StartServer();
	void StartClient();
};

#pragma once

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include <vector>

#include "Templates.h"

using boost::asio::ip::udp;

#define MIN_BUFFER_SIZE 4


namespace Loom
{
	struct NetworkObject
	{
		bool shouldStop = false;
		static inline std::vector<NetworkObject*> networkObjects;

		template <size_t size>
		void ResizeSendBuffer()
		{
			static_assert(size >= MIN_BUFFER_SIZE);
			sendBuffer.resize(size);
		};

		template <size_t size>
		void ResizeRecvBuffer()
		{
			static_assert(size >= MIN_BUFFER_SIZE);
			recvBuffer.resize(size);
		};

		NetworkObject()
		{
			networkObjects.push_back(this);
		};

		~NetworkObject()
		{
			networkObjects.erase(std::remove(networkObjects.begin(), networkObjects.end(), this));
		};

	protected:
		std::vector<char> recvBuffer;
		std::vector<char> sendBuffer;
	};

	struct Server : public NetworkObject
	{
		Server();
	};

	struct Client : public NetworkObject
	{
		Client();
	};
};

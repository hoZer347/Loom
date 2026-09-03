#pragma once

#include "Server.h"


namespace Loom
{
	struct UDPServer final : public Server, public Component<UDPServer>
	{
		template <typename DataPackage>
		void Receive(const DataPackage* data)
		{ };
		
		template <typename DataPackage>
		void Send(const DataPackage* data)
		{ };

		void OnAttach() override;

	private:
		boost::asio::io_context io_context{ };
		boost::asio::ip::udp::socket socket
		{
			io_context,
			boost::asio::ip::udp::endpoint(
				boost::asio::ip::udp::v4(),
				UDP_HOST_IP_PORT)
		};
	};
};

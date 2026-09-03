#pragma once

#include "Component.h"
#include "Server.h"
#include "DataPackage.h"


namespace Loom
{
	struct TCPServer final : public Server, public Component<TCPServer>
	{
		template <typename _Send>
		static inline void Send(const _Send* package)
		{
			std::thread([package]()
				{
					
				}).detach();
		};

		void OnAttach() override;
		void OnUpdate() override;

	private:
		boost::asio::io_context io_context{ };
		boost::asio::ip::tcp::acceptor acceptor
		{
			io_context,
			boost::asio::ip::tcp::endpoint(
				boost::asio::ip::tcp::v4(),
				TCP_HOST_IP_PORT)
		};
	};
};

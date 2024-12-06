module;

#include "Server.h"

export module Server;

namespace Loom
{
	// Boost, and by extension, this, don't work with C++ 20+, implement this once they do
	// For now, use #include "../Loom Networking/Server.h"

	export struct Server;
	export struct TCPServer;
	export struct UDPServer;
};

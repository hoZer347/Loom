import Networking;

#include "Definitions.hpp"

#include <thread>
#include <mutex>
#include <iostream>
#include <vector>
#include <unordered_map>

namespace Loom
{
	static inline std::recursive_mutex server_mut{ };
	static inline std::recursive_mutex client_mut{ };

	static inline std::unordered_map<MessageType, Handle> handles{ };

	void StartServer()
	{
		
	};

	void StartClient()
	{

	};

	void Send(void* message, size_t size)
	{
		
	};

	void SetHandle(Handle handle, MessageType message_type)
	{
		
	};
};

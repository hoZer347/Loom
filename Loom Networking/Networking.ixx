export module Networking;

import <typeinfo>;
import <memory>;
import "Definitions.hpp";


namespace Loom
{
	export struct Message
	{
		MessageType message_type;
	};

	export typedef void(*Handle)(Message*);

	export void StartServer();
	export void StartClient();
	
	void Send(void* message, size_t size);

	export template <MessageType message_type, typename T>
	inline constexpr void Send(T* message)
	{
		static_assert(std::is_base_of<Message, T>::value);
		((Message*)message)->message_type = message_type;
		Send(message, sizeof(T));
	};

	void SetHandle(Handle handle, MessageType message_type);

	export template <MessageType message_type, Handle handle>
	inline constexpr void SetHandle()
	{
		SetHandle(handle, message_type);
	};
};

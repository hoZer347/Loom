export module Networking;

#include "Definitions.hpp"

import <typeinfo>;


namespace Loom
{
	typedef size_t MessageType;
	typedef size_t RequestType;

	export void OpenUDPServerOnThisThread();
	export void OpenUDPClientOnThisThread();

	export void OpenTCPServerOnThisThread();
	export void OpenTCPClientOnThisThread();

	export void StopUDPServer();
	export void StopUDPClient();

	export void StopTCPServer();
	export void StopTCPClient();

	export const bool& GetIsUDPServerOn();
	export const bool& GetIsUDPClientOn();

	export const bool& GetIsTCPServerOn();
	export const bool& GetIsTCPClientOn();

	export struct Message
	{
		virtual ~Message() = default;

		MessageType message_type;
		size_t size;
	};

	export struct TextMessage : public Message
	{
		char text[MAX_CHUNK_SIZE - sizeof(Message)];
	};

	template <MessageType message_type, typename T>
	constexpr void Send(T* message) noexcept
	{
		static_assert(sizeof(T) <= MAX_CHUNK_SIZE, "Type T too large; increase MAX_CHUNK_SIZE or decrease T size");
		Send(message_type, (Message*)message, sizeof(T));
	};

	export void Send(MessageType message_type, Message* message, size_t size);

	export void SetHandle(MessageType message_type, void(*message_interpretter)(Message*));
};

export module Networking;

#include "Definitions.hpp"

import <typeinfo>;


namespace Loom
{
	typedef size_t MessageType;
	typedef size_t RequestType;

	export void OpenServerOnThisThread();
	export void OpenClientOnThisThread();

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

	export template <MessageType message_type, typename S, typename T>
	constexpr void Request(S* sent, T* receive) noexcept
	{
		static_assert(sizeof(S) <= MAX_CHUNK_SIZE, "Type S too large; increase MAX_CHUNK_SIZE or decrease S size");
		static_assert(sizeof(T) <= MAX_CHUNK_SIZE, "Type T too large; increase MAX_CHUNK_SIZE or decrease T size");
		Request(message_type, (Message*)sent, sizeof(S), (Message*)receive, sizeof(T));
	};

	//export void Request(RequestType request_type, Message* sent, size_t sent_size, Message* receive, size_t receive_size);

	export void SetHandle(MessageType message_type, void(*message_interpretter)(Message*));
};

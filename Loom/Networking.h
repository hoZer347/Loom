#pragma once

#include <typeinfo>

#define LOOM_NULL_REQUEST 0
#define LOOM_TEXT_MESSAGE 1

#ifndef MAX_CHUNK_SIZE
#define MAX_CHUNK_SIZE 1024
#endif

namespace Loom
{
	typedef size_t MessageType;
	typedef size_t RequestType;

	void StartServer();
	void StartClient();

	struct Message
	{
		virtual ~Message() = default;

		MessageType message_type;
		size_t size;
	};

	struct TextMessage : public Message
	{
		char text[MAX_CHUNK_SIZE-sizeof(Message)];
	};

	template <MessageType message_type, typename T>
	constexpr void Send(T* message) noexcept
	{
		static_assert(sizeof(T) <= MAX_CHUNK_SIZE, "Type T too large; increase MAX_CHUNK_SIZE or decrease T size");
		Send(message_type, (Message*)message, sizeof(T));
	};

	void Send(MessageType message_type, Message* message, size_t size);

	template <MessageType message_type, typename S, typename T>
	constexpr void Request(S* sent, T* receive) noexcept
	{
		static_assert(sizeof(S) <= MAX_CHUNK_SIZE, "Type S too large; increase MAX_CHUNK_SIZE or decrease S size");
		static_assert(sizeof(T) <= MAX_CHUNK_SIZE, "Type T too large; increase MAX_CHUNK_SIZE or decrease T size");
		Request(message_type, (Message*)sent, sizeof(S), (Message*)receive, sizeof(T));
	};

	void Request(RequestType request_type, Message* sent, size_t sent_size, Message* receive, size_t receive_size);

	void SetHandle(MessageType message_type, void(*message_interpretter)(Message*));
};

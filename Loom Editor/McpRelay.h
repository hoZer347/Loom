#pragma once

#include "McpProtocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>


namespace Loom
{
	/**
	* Loom::McpRelay
	* - The sockets under EditorMcp, so that every open editor answers on one
	*   port. Each serves its own tools on a port of its own and notes that
	*   port in a folder kept for the shared one. Whichever holds the shared
	*   port answers there for them all, passing each call to the editor its
	*   editor argument names, and another takes the port over when it closes
	* - Streamable HTTP at http://127.0.0.1:<port>/mcp, answering each POST
	*   with one JSON response and offering no event stream. A request with an
	*   Origin that is not this machine, which is a web page, is turned away
	* - Each request is answered on a thread of its own, so that a slow call to
	*   one editor holds up no other
	*/
	struct McpRelay final
	{
		#pragma region Lifetime

		// describe names the tool whose text editors shows for each editor.
		McpRelay(nlohmann::json tools, std::string instructions, std::string describe, McpProtocol::CallTool call);
		~McpRelay();

		McpRelay(const McpRelay&) = delete;
		McpRelay& operator=(const McpRelay&) = delete;

		#pragma endregion

		#pragma region Server

		// False, with why on stderr, when there is no port of its own to be
		// had. Not having the shared one yet is no failure.
		bool Listen(unsigned short shared_port);

		// What the editor argument calls this editor.
		unsigned short OwnPort() const { return m_ownPort; };

		// Whether this editor answers on the shared port for them all.
		bool HoldsShared() const { return m_shared != Invalid; };

		// One POST to /mcp on this machine, answered with the response's body,
		// empty when nothing answered.
		static std::string Post(unsigned short port, const std::string& body);

		#pragma endregion

	private:
		#pragma region Server

		void Serve();
		void TakeShared();
		void Spawn(uintptr_t client, bool shared);
		void Answer(uintptr_t client, bool shared);

		// Post's, with the connection closed should this relay stop meanwhile.
		std::string Forward(unsigned short port, const std::string& body);

		// False when the relay is stopping, and the socket is closed.
		bool Track(uintptr_t connection);
		void Untrack(uintptr_t connection);

		#pragma endregion

		#pragma region Relaying

		nlohmann::json SharedTools() const;
		std::optional<nlohmann::json> Relay(const std::string& name, nlohmann::json arguments);

		// The open editors' own ports, this one's among them, forgetting any
		// whose process has ended.
		std::vector<unsigned short> Editors();
		std::string Listing();
		nlohmann::json CallOn(unsigned short port, const std::string& name, const nlohmann::json& arguments);

		#pragma endregion

		#pragma region State

		static constexpr uintptr_t Invalid = ~(uintptr_t)0;

		const nlohmann::json m_tools;
		const std::string m_instructions;
		const std::string m_describe;
		const McpProtocol::CallTool m_call;

		unsigned short m_sharedPort = 0;
		unsigned short m_ownPort = 0;
		std::filesystem::path m_registry{ };

		std::atomic<uintptr_t> m_own = Invalid;
		std::atomic<uintptr_t> m_shared = Invalid;
		bool m_started = false;

		std::thread m_thread{ };
		std::atomic<bool> m_stopping = false;

		// The connections being answered or relayed, which stopping cuts.
		std::mutex m_mutex{ };
		std::condition_variable m_released{ };
		std::set<uintptr_t> m_connections{ };
		size_t m_answering = 0;

		#pragma endregion
	};
};

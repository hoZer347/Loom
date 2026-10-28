// Before windows.h, which would otherwise pull in the old winsock.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include "McpRelay.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;


namespace Loom
{
	namespace
	{
		#pragma region Constants

		constexpr const char* endpoint = "/mcp";

		constexpr size_t max_request_bytes = 1 << 20;
		constexpr size_t receive_buffer_bytes = 4096;
		constexpr std::string_view header_end = "\r\n\r\n";
		constexpr DWORD receive_timeout_ms = 5000;
		constexpr int listen_backlog = 8;

		// Past EditorMcp's own five minutes for a call, so that it answers first.
		constexpr DWORD relay_timeout_ms = 6 * 60 * 1000;

		// How often an editor without the shared port tries to take it.
		constexpr long take_over_seconds = 1;

		constexpr const char* editor_argument = "editor";
		constexpr const char* editors_tool = "editors";

		constexpr const char* shared_instructions =
			" Every open editor answers here: editors lists them, and each tool's editor argument picks one when more than one is open.";

		#pragma endregion

		#pragma region Sockets

		SOCKET Bound(unsigned short port)
		{
			const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			sockaddr_in address{ };
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

			if (listener == INVALID_SOCKET ||
				bind(listener, (const sockaddr*)&address, sizeof(address)) != 0 ||
				listen(listener, listen_backlog) != 0)
			{
				if (listener != INVALID_SOCKET)
					closesocket(listener);

				return INVALID_SOCKET;
			};

			return listener;
		};

		unsigned short PortOf(SOCKET listener)
		{
			sockaddr_in address{ };
			int size = sizeof(address);

			getsockname(listener, (sockaddr*)&address, &size);

			return ntohs(address.sin_port);
		};

		SOCKET Connected(unsigned short port)
		{
			const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			sockaddr_in address{ };
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

			if (server != INVALID_SOCKET && connect(server, (const sockaddr*)&address, sizeof(address)) == 0)
				return server;

			if (server != INVALID_SOCKET)
				closesocket(server);

			return INVALID_SOCKET;
		};

		bool SendAll(SOCKET to, const std::string& data)
		{
			size_t sent = 0;

			while (sent < data.size())
			{
				const int count = send(to, data.data() + sent, (int)(data.size() - sent), 0);

				if (count <= 0)
					return false;

				sent += count;
			};

			return true;
		};

		#pragma endregion

		#pragma region HTTP

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
			return text;
		};

		void Send(SOCKET client, const char* status, const std::string& body, const char* extra_headers = "")
		{
			SendAll(client,
				std::string("HTTP/1.1 ") + status + "\r\n"
				"Content-Type: application/json\r\n"
				"Content-Length: " + std::to_string(body.size()) + "\r\n"
				"Connection: close\r\n" + extra_headers + "\r\n" + body);
		};

		#pragma endregion

		#pragma region Registry

		// One empty file per editor, named by its own port.
		std::filesystem::path RegistryOf(unsigned short shared_port)
		{
			std::error_code error{ };
			std::filesystem::path folder = std::filesystem::temp_directory_path(error) / "Loom MCP" / std::to_string(shared_port);

			std::filesystem::create_directories(folder, error);

			return folder;
		};

		std::vector<unsigned short> Registered(const std::filesystem::path& registry)
		{
			constexpr unsigned long highest_port = 65535;

			std::vector<unsigned short> ports{ };
			std::error_code error{ };

			for (const auto& entry : std::filesystem::directory_iterator(registry, error))
			{
				const std::string name = entry.path().filename().string();
				char* end = nullptr;
				const unsigned long port = strtoul(name.c_str(), &end, 10);

				if (!name.empty() && *end == '\0' && port > 0 && port <= highest_port)
					ports.push_back((unsigned short)port);
			};

			std::sort(ports.begin(), ports.end());

			return ports;
		};

		#pragma endregion
	};

	#pragma region Lifetime

	McpRelay::McpRelay(json tools, std::string instructions, std::string describe, McpProtocol::CallTool call) :
		m_tools(std::move(tools)),
		m_instructions(std::move(instructions)),
		m_describe(std::move(describe)),
		m_call(std::move(call))
	{
	};

	McpRelay::~McpRelay()
	{
		m_stopping = true;

		if (m_thread.joinable())
			m_thread.join();

		if (m_own != invalid)
			closesocket(m_own);

		if (m_shared != invalid)
			closesocket(m_shared);

		if (!m_registry.empty())
		{
			std::error_code error{ };
			std::filesystem::remove(m_registry / std::to_string(m_ownPort), error);
		};

		if (m_started)
			WSACleanup();
	};

	#pragma endregion

	#pragma region Server

	bool McpRelay::Listen(unsigned short shared_port)
	{
		WSADATA data{ };

		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			std::cerr << "The MCP server could not start Windows Sockets" << std::endl;
			return false;
		};

		m_started = true;

		// Any free port will do for its own.
		const SOCKET own = Bound(0);

		if (own == INVALID_SOCKET)
		{
			std::cerr << "The MCP server could not listen on a port of its own" << std::endl;
			return false;
		};

		m_own = own;
		m_ownPort = PortOf(own);
		m_sharedPort = shared_port;
		m_registry = RegistryOf(shared_port);

		std::ofstream(m_registry / std::to_string(m_ownPort));

		TakeShared();

		m_thread = std::thread(&McpRelay::Serve, this);

		return true;
	};

	std::string McpRelay::Post(unsigned short port, const std::string& body)
	{
		const SOCKET server = Connected(port);

		if (server == INVALID_SOCKET)
			return "";

		setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, (const char*)&relay_timeout_ms, sizeof(relay_timeout_ms));

		std::string response{ };

		const std::string request =
			std::string("POST ") + endpoint + " HTTP/1.1\r\n"
			"Host: 127.0.0.1\r\n"
			"Content-Type: application/json\r\n"
			"Accept: application/json, text/event-stream\r\n"
			"Content-Length: " + std::to_string(body.size()) + "\r\n"
			"Connection: close\r\n\r\n" + body;

		if (SendAll(server, request))
		{
			char buffer[receive_buffer_bytes];
			int count = 0;

			while ((count = recv(server, buffer, sizeof(buffer), 0)) > 0)
				response.append(buffer, count);
		};

		closesocket(server);

		const size_t head_size = response.find(header_end);

		return head_size == std::string::npos ? "" : response.substr(head_size + header_end.size());
	};

	void McpRelay::Serve()
	{
		while (!m_stopping)
		{
			fd_set ready{ };
			FD_ZERO(&ready);
			FD_SET((SOCKET)m_own, &ready);

			const SOCKET shared = m_shared;

			if (shared != INVALID_SOCKET)
				FD_SET(shared, &ready);

			timeval wait{ take_over_seconds, 0 };

			if (select(0, &ready, nullptr, nullptr, &wait) <= 0)
			{
				if (shared == INVALID_SOCKET)
					TakeShared();

				continue;
			};

			for (const auto& [listener, is_shared] : { std::pair{ (SOCKET)m_own, false }, std::pair{ shared, true } })
			{
				if (listener == INVALID_SOCKET || !FD_ISSET(listener, &ready))
					continue;

				const SOCKET client = accept(listener, nullptr, nullptr);

				if (client == INVALID_SOCKET)
					continue;

				Answer(client, is_shared);
				closesocket(client);
			};
		};
	};

	void McpRelay::TakeShared()
	{
		const SOCKET shared = Bound(m_sharedPort);

		if (shared != INVALID_SOCKET)
			m_shared = shared;
	};

	void McpRelay::Answer(uintptr_t connection, bool shared)
	{
		const SOCKET client = connection;

		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receive_timeout_ms, sizeof(receive_timeout_ms));

		std::string request{ };
		size_t head_size = std::string::npos;
		char buffer[receive_buffer_bytes];

		while ((head_size = request.find(header_end)) == std::string::npos)
		{
			const int count = recv(client, buffer, sizeof(buffer), 0);

			if (count <= 0)
				return;

			request.append(buffer, count);

			if (request.size() > max_request_bytes)
				return Send(client, "413 Content Too Large", "");
		};

		std::istringstream head(request.substr(0, head_size));
		std::string method, target, line;
		head >> method >> target;
		std::getline(head, line);

		size_t content_length = 0;
		std::string origin{ };

		while (std::getline(head, line))
		{
			const size_t colon = line.find(':');

			if (colon == std::string::npos)
				continue;

			const std::string name = Lower(line.substr(0, colon));
			std::string value = line.substr(colon + 1);

			value.erase(0, value.find_first_not_of(' '));

			if (!value.empty() && value.back() == '\r')
				value.pop_back();

			if (name == "content-length")
				content_length = strtoull(value.c_str(), nullptr, 10);
			else if (name == "origin")
				origin = value;
		};

		if (target.substr(0, target.find('?')) != endpoint)
			return Send(client, "404 Not Found", "");

		if (!origin.empty() && !McpProtocol::LocalOrigin(origin))
			return Send(client, "403 Forbidden", "");

		if (method != "POST")
			return Send(client, "405 Method Not Allowed", "", "Allow: POST\r\n");

		if (content_length > max_request_bytes)
			return Send(client, "413 Content Too Large", "");

		std::string body = request.substr(head_size + header_end.size());

		while (body.size() < content_length)
		{
			const int count = recv(client, buffer, sizeof(buffer), 0);

			if (count <= 0)
				return;

			body.append(buffer, count);
		};

		body.resize(content_length);

		const std::string response = shared
			? McpProtocol::Respond(body, SharedTools(), m_instructions + shared_instructions,
				[this](const std::string& name, const json& arguments) { return Relay(name, arguments); })
			: McpProtocol::Respond(body, m_tools, m_instructions, m_call);

		if (response.empty())
			Send(client, "202 Accepted", "");
		else
			Send(client, "200 OK", response);
	};

	#pragma endregion

	#pragma region Relaying

	json McpRelay::SharedTools() const
	{
		json tools = m_tools;

		for (json& tool : tools)
			tool["inputSchema"]["properties"][editor_argument] = {
				{ "type", "string" },
				{ "description", "Which editor, by the id editors gives it; needed only when more than one is open" } };

		tools.push_back({
			{ "name", editors_tool },
			{ "description", "Every open editor, by the id the other tools' editor argument takes, with each one's state." },
			{ "inputSchema", { { "type", "object" }, { "properties", json::object() } } } });

		return tools;
	};

	std::optional<json> McpRelay::Relay(const std::string& name, json arguments)
	{
		if (name == editors_tool)
			return McpProtocol::Text(Listing());

		if (std::none_of(m_tools.begin(), m_tools.end(), [&](const json& tool) { return tool["name"] == name; }))
			return std::nullopt;

		std::string editor{ };

		if (arguments.is_object() && arguments.contains(editor_argument))
		{
			const json& value = arguments[editor_argument];
			editor = value.is_string() ? value.get<std::string>() : value.dump();
			arguments.erase(editor_argument);
		};

		const std::vector<unsigned short> editors = Editors();

		if (editor.empty())
		{
			if (editors.size() != 1)
				return McpProtocol::Failure("More than one editor is open, so editor needs to name one.\n" + Listing());

			return CallOn(editors.front(), name, arguments);
		};

		for (const unsigned short port : editors)
			if (std::to_string(port) == editor)
				return CallOn(port, name, arguments);

		return McpProtocol::Failure("No open editor is " + editor + ".\n" + Listing());
	};

	std::vector<unsigned short> McpRelay::Editors()
	{
		std::vector<unsigned short> open{ };

		for (const unsigned short port : Registered(m_registry))
		{
			// This thread answers its own port, so it would wait on itself.
			if (port == m_ownPort)
			{
				open.push_back(port);
				continue;
			};

			const SOCKET probe = Connected(port);

			if (probe != INVALID_SOCKET)
			{
				closesocket(probe);
				open.push_back(port);
				continue;
			};

			std::error_code error{ };
			std::filesystem::remove(m_registry / std::to_string(port), error);
		};

		return open;
	};

	std::string McpRelay::Listing()
	{
		std::string out{ };

		for (const unsigned short port : Editors())
		{
			out += "Editor " + std::to_string(port) + '\n';

			const json described = CallOn(port, m_describe, json::object());

			for (const json& part : described.value("content", json::array()))
				if (part.value("type", "") == "text")
					out += part.value("text", "");

			out += '\n';
		};

		return out;
	};

	json McpRelay::CallOn(unsigned short port, const std::string& name, const json& arguments)
	{
		if (port == m_ownPort)
		{
			const std::optional<json> result = m_call(name, arguments);
			return result ? *result : McpProtocol::Failure("no tool is called " + name);
		};

		const json request = {
			{ "jsonrpc", "2.0" },
			{ "id", 1 },
			{ "method", "tools/call" },
			{ "params", { { "name", name }, { "arguments", arguments } } } };

		const json response = json::parse(Post(port, request.dump()), nullptr, false);

		if (response.is_object() && response.contains("result"))
			return response["result"];

		if (response.is_object() && response.contains("error"))
			return McpProtocol::Failure(response["error"].value("message", "the editor refused the call"));

		return McpProtocol::Failure("editor " + std::to_string(port) + " did not answer");
	};

	#pragma endregion
};

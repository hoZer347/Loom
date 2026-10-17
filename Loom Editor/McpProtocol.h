#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::McpProtocol
	* - The Model Context Protocol's side of EditorMcp, with no sockets and no
	*   editor in it: JSON-RPC requests in, responses out, for a server that
	*   has tools and nothing else
	*/
	namespace McpProtocol
	{
		// What a tools/call runs, given the tool's name and its arguments.
		// Nothing when there is no such tool.
		using CallTool = std::function<std::optional<nlohmann::json>(const std::string& name, const nlohmann::json& arguments)>;

		// The response to one request, empty when there is none to send: a
		// notification, or a client's answer to a request.
		std::string Respond(
			const std::string& body,
			const nlohmann::json& tools,
			const std::string& instructions,
			const CallTool& call);

		// A tool's result, which the model reads.
		nlohmann::json Text(const std::string& text);
		nlohmann::json Failure(const std::string& why);

		// Whether a request's Origin header is this machine's. A page in a
		// browser sends its own, and can reach a server on this machine.
		bool LocalOrigin(const std::string& origin);

		std::string Base64(const std::vector<char>& bytes);
	};
};

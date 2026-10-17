#include "McpProtocol.h"

#include <algorithm>
#include <string_view>

using json = nlohmann::json;


namespace Loom
{
	namespace
	{
		constexpr const char* server_name = "loom-editor";
		constexpr const char* server_version = "1.0";

		// Newest first; a client asking for one not here is offered the newest.
		const char* const protocol_versions[] = { "2025-06-18", "2025-03-26", "2024-11-05" };

		// JSON-RPC's own error codes.
		constexpr int parse_error = -32700;
		constexpr int invalid_request = -32600;
		constexpr int method_not_found = -32601;
		constexpr int invalid_params = -32602;

		json Error(const json& id, int code, const std::string& message)
		{
			return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
		};

		json Result(const json& id, const json& result)
		{
			return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
		};

		json Answer(const json& message, const json& tools, const std::string& instructions, const McpProtocol::CallTool& call)
		{
			const json& id = message["id"];
			const std::string method = message["method"].get<std::string>();
			const json params = message.value("params", json::object());

			if (method == "initialize")
			{
				const std::string asked = params.value("protocolVersion", "");
				std::string version = protocol_versions[0];

				for (const char* supported : protocol_versions)
					if (asked == supported)
						version = asked;

				return Result(id, {
					{ "protocolVersion", version },
					{ "capabilities", { { "tools", json::object() } } },
					{ "serverInfo", { { "name", server_name }, { "version", server_version } } },
					{ "instructions", instructions } });
			};

			if (method == "ping")
				return Result(id, json::object());

			if (method == "tools/list")
				return Result(id, { { "tools", tools } });

			if (method != "tools/call")
				return Error(id, method_not_found, method + " is not a method this server has");

			const std::string name = params.value("name", "");
			const std::optional<json> result = call(name, params.value("arguments", json::object()));

			if (!result)
				return Error(id, invalid_params, name + " is not a tool this server has");

			return Result(id, *result);
		};
	};

	std::string McpProtocol::Respond(const std::string& body, const json& tools, const std::string& instructions, const CallTool& call)
	{
		const json message = json::parse(body, nullptr, false);

		if (message.is_discarded())
			return Error(nullptr, parse_error, "not JSON").dump();

		if (!message.is_object())
			return Error(nullptr, invalid_request, "not a request").dump();

		if (!message.contains("method") || !message.contains("id"))
			return "";

		try
		{
			return Answer(message, tools, instructions, call).dump();
		}
		// A field of the wrong type, such as params that are not an object.
		catch (const json::exception& error)
		{
			return Error(message["id"], invalid_request, error.what()).dump();
		};
	};

	json McpProtocol::Text(const std::string& text)
	{
		return { { "content", json::array({ { { "type", "text" }, { "text", text } } }) } };
	};

	json McpProtocol::Failure(const std::string& why)
	{
		json result = Text(why);
		result["isError"] = true;

		return result;
	};

	bool McpProtocol::LocalOrigin(const std::string& origin)
	{
		for (const std::string_view local : { "http://127.0.0.1", "http://localhost", "http://[::1]" })
			if (origin.starts_with(local) && (origin.size() == local.size() || origin[local.size()] == ':'))
				return true;

		return false;
	};

	std::string McpProtocol::Base64(const std::vector<char>& bytes)
	{
		constexpr const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		constexpr char padding = '=';
		constexpr size_t group_bytes = 3;
		constexpr size_t group_digits = 4;
		constexpr int digit_bits = 6;
		constexpr unsigned digit_mask = 0x3F;
		constexpr int byte_bits = 8;

		std::string out{ };
		out.reserve((bytes.size() + group_bytes - 1) / group_bytes * group_digits);

		for (size_t i = 0; i < bytes.size(); i += group_bytes)
		{
			const size_t count = std::min(group_bytes, bytes.size() - i);
			unsigned group = 0;

			for (size_t b = 0; b < group_bytes; b++)
				group = (group << byte_bits) | (b < count ? (unsigned char)bytes[i + b] : 0u);

			// A group of n bytes fills n + 1 digits, and padding the rest.
			for (size_t d = 0; d < group_digits; d++)
				out += d <= count ? digits[(group >> (digit_bits * (group_digits - 1 - d))) & digit_mask] : padding;
		};

		return out;
	};
};

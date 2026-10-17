#include "doctest.h"

#include "McpProtocol.h"

#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
	constexpr int invalid_request = -32600;
	constexpr int method_not_found = -32601;
	constexpr int invalid_params = -32602;
	constexpr int parse_error = -32700;

	// A server with one tool, echo, which answers with its arguments.
	std::string Respond(const std::string& body)
	{
		const json tools = json::array({ { { "name", "echo" } } });

		return Loom::McpProtocol::Respond(body, tools, "Instructions",
			[](const std::string& name, const json& arguments) -> std::optional<json>
			{
				if (name != "echo")
					return std::nullopt;

				return Loom::McpProtocol::Text(arguments.dump());
			});
	};

	json Parsed(const std::string& body)
	{
		return json::parse(Respond(body));
	};

	std::vector<char> Bytes(const std::string& text)
	{
		return std::vector<char>(text.begin(), text.end());
	};
};


TEST_SUITE("McpProtocol")
{
	TEST_CASE("initialize offers the version asked for when it has it, and the newest when not")
	{
		const json known = Parsed(R"({ "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": { "protocolVersion": "2025-03-26" } })");
		const json unknown = Parsed(R"({ "jsonrpc": "2.0", "id": 2, "method": "initialize", "params": { "protocolVersion": "1999-01-01" } })");

		CHECK(known["id"] == 1);
		CHECK(known["result"]["protocolVersion"] == "2025-03-26");
		CHECK(known["result"]["capabilities"].contains("tools"));
		CHECK(known["result"]["instructions"] == "Instructions");
		CHECK(unknown["result"]["protocolVersion"] == "2025-06-18");
	};

	TEST_CASE("tools/list lists the tools, and tools/call runs one")
	{
		const json list = Parsed(R"({ "jsonrpc": "2.0", "id": "a", "method": "tools/list" })");
		const json call = Parsed(R"({ "jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": { "name": "echo", "arguments": { "x": 1 } } })");

		CHECK(list["id"] == "a");
		CHECK(list["result"]["tools"][0]["name"] == "echo");
		CHECK(call["result"]["content"][0]["text"] == R"({"x":1})");
	};

	TEST_CASE("a notification and a client's answer get no response")
	{
		CHECK(Respond(R"({ "jsonrpc": "2.0", "method": "notifications/initialized" })").empty());
		CHECK(Respond(R"({ "jsonrpc": "2.0", "id": 4, "result": { } })").empty());
	};

	TEST_CASE("what is not a request is answered with JSON-RPC's errors")
	{
		CHECK(Parsed("not json")["error"]["code"] == parse_error);
		CHECK(Parsed("[1, 2]")["error"]["code"] == invalid_request);
		CHECK(Parsed(R"({ "jsonrpc": "2.0", "id": 5, "method": "resources/list" })")["error"]["code"] == method_not_found);
		CHECK(Parsed(R"({ "jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": { "name": "missing" } })")["error"]["code"] == invalid_params);
	};

	TEST_CASE("fields of the wrong type are an error, not a throw")
	{
		const json params = Parsed(R"({ "jsonrpc": "2.0", "id": 7, "method": "initialize", "params": [ 1 ] })");
		const json method = Parsed(R"({ "jsonrpc": "2.0", "id": 8, "method": 9 })");
		const json name = Parsed(R"({ "jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": { "name": 1 } })");

		CHECK(params["id"] == 7);
		CHECK(params["error"]["code"] == invalid_request);
		CHECK(method["error"]["code"] == invalid_request);
		CHECK(name["error"]["code"] == invalid_request);
	};

	TEST_CASE("a failure is text marked as an error")
	{
		const json failure = Loom::McpProtocol::Failure("why");

		CHECK(failure["content"][0]["text"] == "why");
		CHECK(failure["isError"] == true);
		CHECK_FALSE(Loom::McpProtocol::Text("fine").contains("isError"));
	};

	TEST_CASE("only this machine's origins are local")
	{
		using Loom::McpProtocol::LocalOrigin;

		CHECK(LocalOrigin("http://127.0.0.1"));
		CHECK(LocalOrigin("http://localhost:5173"));
		CHECK(LocalOrigin("http://[::1]:80"));
		CHECK_FALSE(LocalOrigin("http://localhost.example.com"));
		CHECK_FALSE(LocalOrigin("https://example.com"));
		CHECK_FALSE(LocalOrigin("http://127.0.0.1.example.com"));
	};

	TEST_CASE("base64 pads a last group of one or two bytes")
	{
		using Loom::McpProtocol::Base64;

		CHECK(Base64(Bytes("")) == "");
		CHECK(Base64(Bytes("f")) == "Zg==");
		CHECK(Base64(Bytes("fo")) == "Zm8=");
		CHECK(Base64(Bytes("foo")) == "Zm9v");
		CHECK(Base64(Bytes("foobar")) == "Zm9vYmFy");
		CHECK(Base64({ (char)0xFF, (char)0x00, (char)0x80 }) == "/wCA");
	};
};

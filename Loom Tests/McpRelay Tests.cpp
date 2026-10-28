#include "doctest.h"

#include "McpRelay.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace
{
	// Away from the editor's own, so that an open editor is not in the test.
	constexpr unsigned short shared_port = 47899;

	// Longer than the relay takes to notice the shared port is free.
	constexpr std::chrono::seconds take_over_limit{ 5 };
	constexpr std::chrono::milliseconds take_over_poll{ 50 };

	// A port in the registry with no process noted for it, as a crashed editor can leave.
	constexpr unsigned short closed_port = 1;

	// Far longer than a call that holds up nothing else takes.
	constexpr std::chrono::seconds held_limit{ 3 };

	// An editor whose tools, whoami and its state, answer with its name.
	// whoami waits on hold first, when given one.
	std::unique_ptr<Loom::McpRelay> Editor(const std::string& name, std::shared_future<void> hold = { })
	{
		const json tools = json::array({ {
			{ "name", "whoami" },
			{ "inputSchema", { { "type", "object" }, { "properties", json::object() } } } } });

		auto relay = std::make_unique<Loom::McpRelay>(tools, "Instructions", "whoami",
			[name, hold](const std::string& tool, const json& arguments) -> std::optional<json>
			{
				if (tool != "whoami")
					return std::nullopt;

				if (hold.valid() && arguments.contains("held"))
					hold.wait();

				return Loom::McpProtocol::Text(name + (arguments.empty() ? "" : " " + arguments.dump()));
			});

		REQUIRE(relay->Listen(shared_port));

		return relay;
	};

	json Request(unsigned short port, const std::string& method, const json& params = json::object())
	{
		const json request = { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } };
		const std::string response = Loom::McpRelay::Post(port, request.dump());

		return response.empty() ? json{ } : json::parse(response);
	};

	// What a tools/call on the shared port answered, as text.
	std::string Call(const std::string& tool, const json& arguments = json::object())
	{
		const json response = Request(shared_port, "tools/call", { { "name", tool }, { "arguments", arguments } });

		REQUIRE(response.contains("result"));

		return response["result"]["content"][0]["text"].get<std::string>();
	};

	bool Failed(const std::string& tool, const json& arguments = json::object())
	{
		const json response = Request(shared_port, "tools/call", { { "name", tool }, { "arguments", arguments } });

		return response["result"].value("isError", false);
	};

	std::filesystem::path Registry()
	{
		return std::filesystem::temp_directory_path() / "Loom MCP" / std::to_string(shared_port);
	};
};


TEST_SUITE("McpRelay")
{
	TEST_CASE("one editor answers on the shared port and on its own, and needs no editor argument")
	{
		const auto editor = Editor("A");

		CHECK(editor->HoldsShared());
		CHECK(Call("whoami") == "A");

		const json shared = Request(shared_port, "tools/list")["result"]["tools"];
		const json own = Request(editor->OwnPort(), "tools/list")["result"]["tools"];

		REQUIRE(shared.size() == 2);
		CHECK(shared[0]["inputSchema"]["properties"].contains("editor"));
		CHECK(shared[1]["name"] == "editors");
		REQUIRE(own.size() == 1);
		CHECK_FALSE(own[0]["inputSchema"]["properties"].contains("editor"));
	};

	TEST_CASE("with two editors open, each call goes to the one its editor argument names")
	{
		const auto first = Editor("A");
		const auto second = Editor("B");

		CHECK(first->HoldsShared());
		CHECK_FALSE(second->HoldsShared());

		CHECK(Call("whoami", { { "editor", std::to_string(first->OwnPort()) } }) == "A");
		CHECK(Call("whoami", { { "editor", std::to_string(second->OwnPort()) }, { "x", 1 } }) == "B {\"x\":1}");
		CHECK(Call("whoami", { { "editor", second->OwnPort() } }) == "B");

		CHECK(Failed("whoami"));
		CHECK(Failed("whoami", { { "editor", "1" } }));

		const std::string editors = Call("editors");

		CHECK(editors.find("Editor " + std::to_string(first->OwnPort()) + "\nA") != std::string::npos);
		CHECK(editors.find("Editor " + std::to_string(second->OwnPort()) + "\nB") != std::string::npos);
	};

	TEST_CASE("when the editor holding the shared port closes, another takes it over")
	{
		auto first = Editor("A");
		const auto second = Editor("B");

		first.reset();

		const auto start = std::chrono::steady_clock::now();

		while (!second->HoldsShared() && std::chrono::steady_clock::now() - start < take_over_limit)
			std::this_thread::sleep_for(take_over_poll);

		REQUIRE(second->HoldsShared());
		CHECK(Call("whoami") == "B");
	};

	TEST_CASE("a call held up in one editor holds up no other, nor the hub's closing")
	{
		std::promise<void> release{ };
		auto hub = Editor("A");
		const auto slow = Editor("B", release.get_future().share());
		const std::string slow_id = std::to_string(slow->OwnPort());

		auto held = std::async(std::launch::async,
			[&slow_id]()
			{
				return Loom::McpRelay::Post(shared_port, json{
					{ "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/call" },
					{ "params", { { "name", "whoami" }, { "arguments", { { "editor", slow_id }, { "held", true } } } } } }.dump());
			});

		const auto start = std::chrono::steady_clock::now();

		CHECK(Call("whoami", { { "editor", std::to_string(hub->OwnPort()) } }) == "A");

		hub.reset();

		CHECK(std::chrono::steady_clock::now() - start < held_limit);
		CHECK(held.get().empty());

		release.set_value();
	};

	TEST_CASE("an editor whose process has ended is forgotten")
	{
		const auto editor = Editor("A");

		std::ofstream(Registry() / std::to_string(closed_port));

		CHECK(Call("whoami") == "A");
		CHECK_FALSE(std::filesystem::exists(Registry() / std::to_string(closed_port)));
	};

	TEST_CASE("an editor's own port leaves the registry when it closes")
	{
		auto editor = Editor("A");
		const std::filesystem::path own = Registry() / std::to_string(editor->OwnPort());

		CHECK(std::filesystem::exists(own));

		editor.reset();

		CHECK_FALSE(std::filesystem::exists(own));
		CHECK(Loom::McpRelay::Post(shared_port, "{ }").empty());
	};
};

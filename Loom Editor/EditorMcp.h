#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>


namespace Loom
{
	struct Editor;
	struct UiScript;

	/**
	* Loom::EditorMcp
	* - A Model Context Protocol server inside the editor, so an agent can look
	*   at and change it while someone is using it: the open scenes, their
	*   hierarchy and fields, the console, Play and the other commands,
	*   screenshots, and the panels themselves through a UiScript
	* - Streamable HTTP at http://127.0.0.1:<port>/mcp, answering each POST
	*   with one JSON response and offering no event stream. A request with an
	*   Origin that is not this machine, which is a web page, is turned away
	* - Requests are read on a thread of its own. The tools run in Step, on the
	*   main thread between frames, so they see the editor as it was drawn and
	*   their edits are undone like any other
	*/
	struct EditorMcp final
	{
		static constexpr unsigned short default_port = 47800;

		EditorMcp(Editor& editor, UiScript& script);
		~EditorMcp();

		EditorMcp(const EditorMcp&) = delete;
		EditorMcp& operator=(const EditorMcp&) = delete;

		// False, with why on stderr, when the port cannot be had.
		bool Listen(unsigned short port);

		// Once a frame, before ImGui::NewFrame.
		void Step();

		// One tools/call, handed from the server's thread to the main one.
		struct Call;

		// Runs each frame until it answers its call, for a tool that takes
		// frames to finish.
		using Poll = std::function<bool()>;

	private:
		void Serve();
		void Answer(uintptr_t connection);

		// The JSON-RPC response to a request, empty for a notification.
		std::string Respond(const std::string& body);

		// Blocks the server's thread until the main thread has answered.
		void Wait(const std::shared_ptr<Call>& call);
		void Finish(const std::shared_ptr<Call>& call);

		Editor& m_editor;
		UiScript& m_script;

		uintptr_t m_listener;
		std::thread m_thread{ };

		std::mutex m_mutex{ };
		std::condition_variable m_answered{ };
		std::deque<std::shared_ptr<Call>> m_calls{ };
		bool m_stopping = false;

		std::shared_ptr<Call> m_running{ };
		Poll m_poll{ };
	};
};

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>


namespace Loom
{
	struct Editor;
	struct McpRelay;
	struct UiScript;

	/**
	* Loom::EditorMcp
	* - A Model Context Protocol server inside the editor, so an agent can look
	*   at and change it while someone is using it: the open scenes, their
	*   hierarchy and fields, the console, Play and the other commands,
	*   screenshots, and the panels themselves through a UiScript
	* - Every open editor answers on one port through McpRelay, which reads
	*   requests on a thread of its own. The tools run in Step, on the
	*   main thread between frames, so they see the editor as it was drawn and
	*   their edits are undone like any other
	* - The UiScript only hears about ImGui's items while a tool needs them
	*/
	struct EditorMcp final
	{
		#pragma region Lifetime

		EditorMcp(Editor& editor, UiScript& script);
		~EditorMcp();

		EditorMcp(const EditorMcp&) = delete;
		EditorMcp& operator=(const EditorMcp&) = delete;

		#pragma endregion

		#pragma region Server

		static constexpr unsigned short default_port = 47800;

		// False, with why on stderr, when the editor cannot serve at all.
		// Another editor holding the port answers for this one as well.
		bool Listen(unsigned short port);

		#pragma endregion

		#pragma region Call Queue

		// Once a frame, before ImGui::NewFrame.
		void Step();

		// One tools/call, handed from the server's thread to the main one.
		struct Call;

		// Runs each frame until it answers its call, for a tool that takes
		// frames to finish.
		using Poll = std::function<bool()>;

		#pragma endregion

	private:
		#pragma region Call Queue

		// Blocks the server's thread until the main thread has answered.
		nlohmann::json Wait(const std::shared_ptr<Call>& call);
		void Finish(const std::shared_ptr<Call>& call);

		#pragma endregion

		#pragma region State

		Editor& m_editor;
		UiScript& m_script;

		// Whether the script was listening before a tool turned it on.
		const bool m_scriptListened;

		std::unique_ptr<McpRelay> m_relay{ };

		std::mutex m_mutex{ };
		std::condition_variable m_answered{ };
		std::deque<std::shared_ptr<Call>> m_calls{ };
		bool m_stopping = false;

		std::shared_ptr<Call> m_running{ };
		Poll m_poll{ };
		std::chrono::steady_clock::time_point m_runningSince{ };

		#pragma endregion
	};
};

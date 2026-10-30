#pragma once

#include "Engine.h"
#include "ImGuiItemHooks.h"

#include "imgui_internal.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	/**
	* Loom::UiScript
	* - Drives ImGui from a text file, a command a line, by queueing input
	*   events in-process. The real mouse and keyboard are never touched, so it
	*   runs on a hidden window. The game hears the mouse, the keys and the
	*   wheel too, as it would the platform's
	* - Commands, with a line starting # a comment:
	*     wait <frames>
	*     move <target>
	*     click <target>, rightclick <target>, doubleclick <target>
	*     backclick <target>, forwardclick <target>
	*                          the mouse's side buttons
	*     drag <target> to <target>
	*     key <chord>          ImGui's key names joined by +: ctrl+z, enter, f2
	*     hold <chord>         presses it and leaves it down, for a click with ctrl
	*     release <chord>      lets go of a held chord
	*     hold mouse           presses the left button and leaves it down, for
	*                          moves to see what a drag shows on the way
	*     release mouse        lets go of it
	*     scroll <notches>     turns the wheel where the mouse is, away from you
	*                          positive
	*     type <text>          the rest of the line
	*     screenshot <file> [window]
	*                          the whole window as a bitmap, or only the first
	*                          shown window whose name contains window
	*     items [file]         every labelled item drawn last frame, to the file
	*                          or the console: window, label, x, y, width, height
	*     stages <frames> <file>
	*                          records every frame's stages for that many
	*                          frames, then writes the last hundred's to the
	*                          file: a frame row of its average length, as both
	*                          average and peak, then thread, stage, depth,
	*                          average and peak, in milliseconds
	*     resize <width> <height>
	*                          the window, in pixels
	*     quit
	* - A target is x y in pixels from the window's top left, or
	*   "window" "label" [left|right] [dx dy]: the first item with that label in
	*   a window whose name contains window ("" for any), at its centre or just
	*   inside an edge, moved by dx dy. A label matches whole, or up to its ##.
	*   A target not drawn yet is waited for. An item with no label of its own,
	*   like the Hierarchy's eyes, is aimed at through its row:
	*   click "Hierarchy" "Cube" right
	* - The script, and the files screenshot, items and stages write, are
	*   found from the folder it is given, not wherever the working directory
	*   has moved to
	* - The script ends when it runs out of commands, at quit, or at the first
	*   command that fails, which says why on stderr
	*/
	struct UiScript final : ImGuiItemListener
	{
		explicit UiScript(const std::filesystem::path& folder);
		~UiScript();

		UiScript(const UiScript&) = delete;
		UiScript& operator=(const UiScript&) = delete;

		#pragma region Script

		// False, with why on stderr, when the file cannot be read.
		bool Load(const std::string& path);
		void LoadText(const std::string& text);

		// Starts over on these commands, dropping whatever was left to run.
		void Run(const std::string& text);

		// Once a frame, before ImGui::NewFrame, in the context it was made in.
		void Step();

		bool Finished() const { return m_finished; };
		bool Failed() const { return m_failed; };

		#pragma endregion

		#pragma region Items

		// Whether ImGui tells it about the items it draws, which it does from
		// the start. Not hearing them costs nothing, and leaves nothing to aim
		// at from the frame after.
		void Listen(bool listening);
		bool Listening() const;

		// What items lists, from the windows whose names contain window.
		void WriteItems(std::ostream& out, const std::string& window = "") const;

		// What stages writes, from the profiler's last frame.
		static void WriteStages(std::ostream& out);

		// Whether a script is in the middle of stages, so the editor records
		// them whether or not Stats shows them.
		static bool RecordingStages();

		// Where the first window whose name contains window was drawn last
		// frame, in the framebuffer's pixels. None when it is closed, behind
		// another tab or floated out into a viewport of its own.
		static std::optional<Engine::Region> WindowRegion(const std::string& window);

		void ItemAdded(ImGuiID id, const ImRect& rect) override;
		void ItemLabelled(ImGuiID id, const char* label) override;

		#pragma endregion

	private:
		#pragma region Items

		struct Item
		{
			std::string window;
			std::string label;
			ImVec2 min;
			ImVec2 max;
		};

		const Item* Find(const std::string& window, const std::string& label) const;

		#pragma endregion

		#pragma region Commands

		// Retry is for a target not drawn yet: the action runs again next frame.
		enum class Result
		{
			Done,
			Retry
		};

		// One frame's worth of a command.
		using Action = std::function<Result()>;

		bool Parse(const std::string& line);

		// Queues the actions that put the mouse on target, from tokens[first] on.
		bool Aim(const std::vector<std::string>& tokens, size_t first, size_t* end);

		void Fail(const std::string& why);

		// The cursor to m_mouse, for ImGui and the game alike.
		void MoveCursor();

		// Lets go of whatever hold left down, so a script that stops early
		// leaves no key stuck for the person at the keyboard.
		void ReleaseHeld();

		void RecordStages(bool recording);

		#pragma endregion

		ImGuiContext* m_context = nullptr;
		std::filesystem::path m_folder{ };

		std::vector<std::string> m_lines{ };
		size_t m_next = 0;
		size_t m_line = 0;

		std::deque<Action> m_actions{ };
		int m_wait = 0;
		int m_retries = 0;

		std::vector<ImGuiKey> m_held{ };
		bool m_mouseHeld = false;
		bool m_recordingStages = false;
		bool m_everyFrameBefore = false;

		ImVec2 m_mouse{ };
		bool m_mouseSet = false;

		// What the hooks are hearing about this frame, and what they heard
		// about the last whole one, which is what targets are found in.
		std::unordered_map<ImGuiID, ImRect> m_rects{ };
		std::vector<Item> m_drawing{ };
		std::vector<Item> m_drawn{ };

		bool m_finished = false;
		bool m_failed = false;
	};
};

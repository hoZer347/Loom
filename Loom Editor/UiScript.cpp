#include "UiScript.h"

#include "Engine.h"
#include "Input.h"
#include "OpenGL.h"
#include "Profiler.h"
#include "TabHistory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>


// ImGui's GLFW backend leaves this out of its header.
ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);


namespace Loom
{
	namespace
	{
		#pragma region Commands

		// Words and "quoted strings", the quotes taken off.
		std::vector<std::string> Tokens(const std::string& line)
		{
			std::vector<std::string> tokens{ };
			size_t i = 0;

			while (i < line.size())
			{
				if (isspace((unsigned char)line[i]))
				{
					i++;
					continue;
				};

				if (line[i] == '"')
				{
					const size_t close = line.find('"', i + 1);
					const size_t end = close == std::string::npos ? line.size() : close;

					tokens.push_back(line.substr(i + 1, end - i - 1));
					i = end + 1;
					continue;
				};

				size_t end = i;

				while (end < line.size() && !isspace((unsigned char)line[end]))
					end++;

				tokens.push_back(line.substr(i, end - i));
				i = end;
			};

			return tokens;
		};

		bool Number(const std::string& token, float* value)
		{
			if (token.empty())
				return false;

			char* end = nullptr;
			*value = strtof(token.c_str(), &end);

			return *end == '\0' && std::isfinite(*value);
		};

		#pragma endregion

		#pragma region Keys

		struct Modifier
		{
			const char* name;
			ImGuiKey flag;
			ImGuiKey key;
		};

		const Modifier modifiers[] =
		{
			{ "ctrl", ImGuiMod_Ctrl, ImGuiKey_LeftCtrl },
			{ "shift", ImGuiMod_Shift, ImGuiKey_LeftShift },
			{ "alt", ImGuiMod_Alt, ImGuiKey_LeftAlt },
			{ "super", ImGuiMod_Super, ImGuiKey_LeftSuper },
		};

		// The backend only maps GLFW to ImGui, so it is inverted over every key once.
		int GameKey(ImGuiKey key)
		{
			static const std::unordered_map<ImGuiKey, int> numbers = []()
			{
				std::unordered_map<ImGuiKey, int> found{ };

				for (int number = 0; number < Input::keys; number++)
					if (const ImGuiKey named = ImGui_ImplGlfw_KeyToImGuiKey(number, 0); named != ImGuiKey_None)
						found.try_emplace(named, number);

				return found;
			}();

			for (const Modifier& modifier : modifiers)
				if (key == modifier.flag)
					key = modifier.key;

			const auto found = numbers.find(key);

			return found == numbers.end() ? -1 : found->second;
		};

		ImGuiKey KeyNamed(const std::string& name)
		{
			for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++)
				if (ImStricmp(ImGui::GetKeyName((ImGuiKey)key), name.c_str()) == 0)
					return (ImGuiKey)key;

			return ImGuiKey_None;
		};

		#pragma endregion

		#pragma region Targets

		// How long a target that has not been drawn yet is waited for.
		constexpr int target_wait_frames = 120;

		// How far inside an item's left or right edge "left" and "right" aim.
		constexpr float edge_inset = 4.0f;

		// Past ImGui's drag threshold, so the drag has started.
		constexpr float drag_nudge = 10.0f;

		// The part of a label ImGui shows: everything before its ##.
		std::string Shown(const std::string& label)
		{
			return label.substr(0, label.find("##"));
		};

		#pragma endregion

		// The scripts in the middle of stages.
		std::atomic<int> s_recordingStages{ 0 };
	};

	UiScript::UiScript(const std::filesystem::path& folder) :
		m_context(ImGui::GetCurrentContext()),
		m_folder(folder)
	{
		m_context->TestEngine = this;
		m_context->TestEngineHookItems = true;
	};

	UiScript::~UiScript()
	{
		RecordStages(false);

		if (m_context->TestEngine != this)
			return;

		m_context->TestEngine = nullptr;
		m_context->TestEngineHookItems = false;
	};

	#pragma region Script

	bool UiScript::Load(const std::string& path)
	{
		std::ifstream file(m_folder / path);

		if (!file)
		{
			std::cerr << "Could not read the UI script " << path << std::endl;
			return false;
		};

		std::stringstream text;
		text << file.rdbuf();

		LoadText(text.str());

		return true;
	};

	void UiScript::LoadText(const std::string& text)
	{
		std::istringstream lines(text);
		std::string line;

		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			m_lines.push_back(line);
		};
	};

	void UiScript::Run(const std::string& text)
	{
		ReleaseHeld();
		RecordStages(false);

		m_next = m_lines.size();
		m_actions.clear();
		m_wait = 0;
		m_retries = 0;
		m_finished = false;
		m_failed = false;

		LoadText(text);
	};

	void UiScript::Step()
	{
		m_drawn.swap(m_drawing);
		m_drawing.clear();
		m_rects.clear();

		if (m_finished)
			return;

		// After the platform's queue, so this frame reads the script's position.
		if (m_mouseSet)
			MoveCursor();

		if (m_wait > 0)
		{
			m_wait--;
			return;
		};

		while (m_actions.empty())
		{
			if (m_next >= m_lines.size())
			{
				ReleaseHeld();
				m_finished = true;
				return;
			};

			m_line = m_next++;

			if (!Parse(m_lines[m_line]))
				return;
		};

		const Result result = m_actions.front()();

		if (m_finished)
			return;

		if (result == Result::Done)
		{
			m_actions.pop_front();
			m_retries = 0;
		}
		else if (++m_retries > target_wait_frames)
			Fail("its target was not drawn within " + std::to_string(target_wait_frames) + " frames");
	};

	#pragma endregion

	#pragma region Items

	void UiScript::Listen(bool listening)
	{
		if (m_context->TestEngine == this)
			m_context->TestEngineHookItems = listening;
	};

	bool UiScript::Listening() const
	{
		return m_context->TestEngine == this && m_context->TestEngineHookItems;
	};

	void UiScript::ItemAdded(ImGuiID id, const ImRect& rect)
	{
		m_rects[id] = rect;
	};

	void UiScript::ItemLabelled(ImGuiID id, const char* label)
	{
		const auto rect = m_rects.find(id);

		if (rect == m_rects.end() || m_context->CurrentWindow == nullptr)
			return;

		m_drawing.push_back({ m_context->CurrentWindow->Name, label, rect->second.Min, rect->second.Max });
	};

	void UiScript::WriteItems(std::ostream& out, const std::string& window) const
	{
		const ImVec2 origin = ImGui::GetMainViewport()->Pos;

		for (const Item& item : m_drawn)
			if (item.window.find(window) != std::string::npos)
				out
					<< item.window << '\t'
					<< item.label << '\t'
					<< item.min.x - origin.x << '\t'
					<< item.min.y - origin.y << '\t'
					<< item.max.x - item.min.x << '\t'
					<< item.max.y - item.min.y << '\n';

		out.flush();
	};

	void UiScript::WriteStages(std::ostream& out)
	{
#ifndef LOOM_FINAL
		out << "Frame\t\t0\t" << Profiler::FrameLength() << '\t' << Profiler::FrameLength() << '\n';

		for (const Profiler::Thread& thread : Profiler::Threads())
			for (const Profiler::Row& row : thread.rows)
				out
					<< thread.name << '\t'
					<< row.name << '\t'
					<< row.depth << '\t'
					<< row.average << '\t'
					<< row.peak << '\n';
#endif

		out.flush();
	};

	std::optional<Engine::Region> UiScript::WindowRegion(const std::string& window)
	{
		const ImVec2 origin = ImGui::GetMainViewport()->Pos;
		const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;

		for (const ImGuiWindow* shown : ImGui::GetCurrentContext()->Windows)
			if (std::string(shown->Name).find(window) != std::string::npos &&
				((shown->Flags & ImGuiWindowFlags_ChildWindow) == 0 || shown->DockIsActive) &&
				shown->Viewport == ImGui::GetMainViewport() &&
				shown->WasActive &&
				!shown->Hidden)
				return Engine::Region{
					(int)((shown->Pos.x - origin.x) * scale.x),
					(int)((shown->Pos.y - origin.y) * scale.y),
					(int)(shown->Size.x * scale.x),
					(int)(shown->Size.y * scale.y) };

		return std::nullopt;
	};

	const UiScript::Item* UiScript::Find(const std::string& window, const std::string& label) const
	{
		for (const Item& item : m_drawn)
			if (item.window.find(window) != std::string::npos &&
				(item.label == label || Shown(item.label) == label))
				return &item;

		return nullptr;
	};

	#pragma endregion

	#pragma region Commands

	bool UiScript::Parse(const std::string& line)
	{
		const std::vector<std::string> tokens = Tokens(line);

		if (tokens.empty() || tokens[0][0] == '#')
			return true;

		const std::string& verb = tokens[0];
		ImGuiIO& io = ImGui::GetIO();

		const auto press =
			[this, &io](ImGuiMouseButton button, bool down)
			{
				m_actions.push_back(
					[&io, button, down]()
					{
						// The game hears it too, as it would a real one.
						io.AddMouseButtonEvent(button, down);
						Input::OnMouseButtonThisFrame(button, down);
						return Result::Done;
					});
			};

		const auto aim_only =
			[&]() -> bool
			{
				size_t end = 0;

				if (!Aim(tokens, 1, &end))
					return false;

				if (end != tokens.size())
				{
					Fail("there is more after the target than it takes");
					return false;
				};

				return true;
			};

		if (verb == "wait")
		{
			float frames = 0.0f;

			// The float nearest the most an int holds is one past it.
			if (tokens.size() != 2 || !Number(tokens[1], &frames) || frames < 1.0f || frames >= (float)std::numeric_limits<int>::max())
			{
				Fail("wait takes a number of frames");
				return false;
			};

			m_actions.push_back(
				[this, frames]()
				{
					// This frame is the first of them.
					m_wait = (int)frames - 1;
					return Result::Done;
				});
		}
		else if (verb == "move")
			return aim_only();
		else if (verb == "click" || verb == "rightclick" || verb == "doubleclick" || verb == "backclick" || verb == "forwardclick")
		{
			if (!aim_only())
				return false;

			ImGuiMouseButton button = ImGuiMouseButton_Left;

			if (verb == "rightclick")
				button = ImGuiMouseButton_Right;
			else if (verb == "backclick")
				button = TabHistory::back_button;
			else if (verb == "forwardclick")
				button = TabHistory::forward_button;

			const int clicks = verb == "doubleclick" ? 2 : 1;

			for (int click = 0; click < clicks; click++)
			{
				press(button, true);
				press(button, false);
			};
		}
		else if (verb == "drag")
		{
			size_t to = 0;

			if (!Aim(tokens, 1, &to))
				return false;

			if (to >= tokens.size() || tokens[to] != "to")
			{
				Fail("drag takes a target, then to and another target");
				return false;
			};

			press(ImGuiMouseButton_Left, true);

			m_actions.push_back(
				[this]()
				{
					m_mouse.x += drag_nudge;
					MoveCursor();
					return Result::Done;
				});

			size_t end = 0;

			if (!Aim(tokens, to + 1, &end))
				return false;

			if (end != tokens.size())
			{
				Fail("there is more after the target than it takes");
				return false;
			};

			// A frame over the drop before letting go, for it to see the drag.
			m_actions.push_back([]() { return Result::Done; });

			press(ImGuiMouseButton_Left, false);
		}
		else if ((verb == "hold" || verb == "release") && tokens.size() == 2 && tokens[1] == "mouse")
		{
			const bool down = verb == "hold";

			press(ImGuiMouseButton_Left, down);
			m_actions.push_back(
				[this, down]()
				{
					m_mouseHeld = down;
					return Result::Done;
				});
		}
		else if (verb == "key" || verb == "hold" || verb == "release")
		{
			if (tokens.size() != 2)
			{
				Fail(verb + " takes one chord, such as ctrl+z");
				return false;
			};

			std::vector<ImGuiKey> held{ };
			std::stringstream parts(tokens[1]);
			std::string part;

			while (std::getline(parts, part, '+'))
			{
				ImGuiKey key = ImGuiKey_None;

				for (const Modifier& modifier : modifiers)
					if (ImStricmp(modifier.name, part.c_str()) == 0)
						key = modifier.flag;

				if (key == ImGuiKey_None)
					key = KeyNamed(part);

				if (key == ImGuiKey_None)
				{
					Fail("there is no key called " + part);
					return false;
				};

				held.push_back(key);
			};

			// The game hears them too, as it would real ones.
			if (verb != "release")
				m_actions.push_back(
					[this, &io, held]()
					{
						for (ImGuiKey key : held)
						{
							io.AddKeyEvent(key, true);
							Input::OnKeyThisFrame(GameKey(key), true);
							m_held.push_back(key);
						};

						return Result::Done;
					});

			if (verb != "hold")
				m_actions.push_back(
					[this, &io, held]()
					{
						for (auto key = held.rbegin(); key != held.rend(); key++)
						{
							io.AddKeyEvent(*key, false);
							Input::OnKeyThisFrame(GameKey(*key), false);
							m_held.erase(std::remove(m_held.begin(), m_held.end(), *key), m_held.end());
						};

						return Result::Done;
					});
		}
		else if (verb == "scroll")
		{
			float notches = 0.0f;

			if (tokens.size() != 2 || !Number(tokens[1], &notches))
			{
				Fail("scroll takes a number of notches, away from you positive");
				return false;
			};

			m_actions.push_back(
				[&io, notches]()
				{
					io.AddMouseWheelEvent(0.0f, notches);
					Input::OnMouseWheelThisFrame(notches);
					return Result::Done;
				});
		}
		else if (verb == "type")
		{
			const size_t start = line.find_first_not_of(" \t", line.find(verb) + verb.size());
			const std::string text = start == std::string::npos ? "" : line.substr(start);

			m_actions.push_back(
				[&io, text]()
				{
					io.AddInputCharactersUTF8(text.c_str());
					return Result::Done;
				});
		}
		else if (verb == "screenshot" && (tokens.size() == 2 || tokens.size() == 3))
		{
			const std::string path = (m_folder / tokens[1]).string();
			const std::optional<std::string> window = tokens.size() == 3 ? std::optional(tokens[2]) : std::nullopt;

			m_actions.push_back(
				[this, path, window]()
				{
					if (!window)
					{
						Engine::CaptureFrame(path);
						return Result::Done;
					};

					const std::optional<Engine::Region> region = WindowRegion(*window);

					if (region)
						Engine::CaptureFrame(path, region);
					else Fail("no window whose name contains \"" + *window + "\" is showing");

					return Result::Done;
				});
		}
		else if (verb == "items" && tokens.size() <= 2)
		{
			const std::string path = tokens.size() == 2 ? (m_folder / tokens[1]).string() : "";

			m_actions.push_back(
				[this, path]()
				{
					if (path.empty())
					{
						WriteItems(std::cout);
						return Result::Done;
					};

					std::ofstream file(path);

					if (!file)
					{
						Fail("could not write " + path);
						return Result::Done;
					};

					WriteItems(file);
					return Result::Done;
				});
		}
		else if (verb == "stages")
		{
			float frames = 0.0f;

			if (tokens.size() != 3 || !Number(tokens[1], &frames) || frames < 1.0f || frames >= (float)std::numeric_limits<int>::max())
			{
				Fail("stages takes a number of frames and a file");
				return false;
			};

			const std::string path = (m_folder / tokens[2]).string();

			m_actions.push_back(
				[this, frames]()
				{
					RecordStages(true);
					m_wait = (int)frames - 1;
					return Result::Done;
				});

			m_actions.push_back(
				[this, path]()
				{
					RecordStages(false);

					std::ofstream file(path);

					if (!file)
					{
						Fail("could not write " + path);
						return Result::Done;
					};

					WriteStages(file);
					return Result::Done;
				});
		}
		else if (verb == "resize")
		{
			float width = 0.0f;
			float height = 0.0f;

			if (tokens.size() != 3 || !Number(tokens[1], &width) || !Number(tokens[2], &height) || width < 1.0f || height < 1.0f)
			{
				Fail("resize takes a width and a height in pixels");
				return false;
			};

			m_actions.push_back(
				[this, width, height]()
				{
					if (Engine::window)
						glfwSetWindowSize(Engine::window, (int)width, (int)height);
					else Fail("there is no window to resize");

					return Result::Done;
				});
		}
		else if (verb == "quit" && tokens.size() == 1)
		{
			m_actions.push_back(
				[this]()
				{
					ReleaseHeld();
					m_finished = true;
					return Result::Done;
				});
		}
		else
		{
			Fail("not a command, or not with those arguments");
			return false;
		};

		return true;
	};

	bool UiScript::Aim(const std::vector<std::string>& tokens, size_t first, size_t* end)
	{
		float x = 0.0f;
		float y = 0.0f;

		if (first + 1 < tokens.size() && Number(tokens[first], &x) && Number(tokens[first + 1], &y))
		{
			*end = first + 2;

			m_actions.push_back(
				[this, x, y]()
				{
					const ImVec2 origin = ImGui::GetMainViewport()->Pos;

					m_mouse = ImVec2(origin.x + x, origin.y + y);
					m_mouseSet = true;
					MoveCursor();
					return Result::Done;
				});

			return true;
		};

		if (first + 1 >= tokens.size())
		{
			Fail("a target is x y, or \"window\" \"label\"");
			return false;
		};

		const std::string window = tokens[first];
		const std::string label = tokens[first + 1];
		size_t next = first + 2;

		float side = 0.0f;

		if (next < tokens.size() && (tokens[next] == "left" || tokens[next] == "right"))
		{
			side = tokens[next] == "left" ? -1.0f : 1.0f;
			next++;
		};

		ImVec2 offset{ };

		if (next + 1 < tokens.size() && Number(tokens[next], &offset.x) && Number(tokens[next + 1], &offset.y))
			next += 2;

		*end = next;

		m_actions.push_back(
			[this, window, label, side, offset]()
			{
				const Item* item = Find(window, label);

				if (item == nullptr)
					return Result::Retry;

				const ImRect rect(item->min, item->max);
				ImVec2 point = rect.GetCenter();

				if (side < 0.0f)
					point.x = rect.Min.x + edge_inset;
				else if (side > 0.0f)
					point.x = rect.Max.x - edge_inset;

				m_mouse = ImVec2(point.x + offset.x, point.y + offset.y);
				m_mouseSet = true;
				MoveCursor();
				return Result::Done;
			});

		return true;
	};

	void UiScript::MoveCursor()
	{
		ImGui::GetIO().AddMousePosEvent(m_mouse.x, m_mouse.y);

		// The game hears it too, from the window's corner, as it would the platform's.
		const ImGuiViewport* window = ImGui::GetMainViewport();

		Input::OnMouseMove((int)(m_mouse.x - window->Pos.x), (int)(m_mouse.y - window->Pos.y), (int)window->Size.x, (int)window->Size.y);
	};

	void UiScript::ReleaseHeld()
	{
		for (auto key = m_held.rbegin(); key != m_held.rend(); key++)
		{
			m_context->IO.AddKeyEvent(*key, false);
			Input::OnKeyThisFrame(GameKey(*key), false);
		};

		m_held.clear();

		if (m_mouseHeld)
		{
			m_context->IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Input::OnMouseButtonThisFrame(ImGuiMouseButton_Left, false);
			m_mouseHeld = false;
		};
	};

	void UiScript::RecordStages(bool recording)
	{
		if (m_recordingStages == recording)
			return;

		m_recordingStages = recording;
		s_recordingStages += recording ? 1 : -1;

#ifndef LOOM_FINAL
		if (recording)
			m_everyFrameBefore = Profiler::EveryFrame();

		Profiler::SetEveryFrame(recording || m_everyFrameBefore);
#endif
	};

	bool UiScript::RecordingStages()
	{
		return s_recordingStages > 0;
	};

	void UiScript::Fail(const std::string& why)
	{
		std::cerr
			<< "UI script line " << m_line + 1 << ", " << m_lines[m_line] << ": "
			<< why << std::endl;

		ReleaseHeld();
		RecordStages(false);

		m_actions.clear();
		m_finished = true;
		m_failed = true;
	};

	#pragma endregion
};

#include "ShaderEditor.h"

#include "EditorIcons.h"

// The editor only runs on Windows, but the web test build compiles this too,
// and glslang is desktop only.
#ifndef __EMSCRIPTEN__

#include "EditorTheme.h"
#include "ProjectAssets.h"
#include "ShaderVariables.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
#include <utility>


namespace Loom
{
	namespace
	{
		// After the last keystroke, before the text is checked.
		constexpr int check_delay_milliseconds = 300;

		constexpr size_t tab_size = 4;

		// The squiggle type glslang's messages are drawn with.
		constexpr size_t message_squiggles = 1;

		// A window docked on the frame it first appears only takes its tab on
		// the one after.
		constexpr int focus_frames = 2;

		// How strongly a line with a message on it is tinted.
		constexpr float marker_alpha = 0.15f;

		// The most messages listed under the text before the list scrolls.
		constexpr size_t max_message_rows = 6;

		// How wide an explanation runs before it wraps, in font heights.
		constexpr float explanation_width = 30.0f;

		constexpr char quote = '\'';

		bool IsIdentifier(char c)
		{
			return std::isalnum((unsigned char)c) || c == '_';
		};

		ImU32 Colour(const ShaderMessage& message, float alpha = 1.0f)
		{
			ImVec4 colour = message.error ? EditorTheme::ErrorText : EditorTheme::WarningText;
			colour.w *= alpha;

			return ImGui::ColorConvertFloat4ToU32(colour);
		};

		const ShaderSymbol* EngineSymbol(std::string_view name)
		{
			const auto found = std::find_if(
				std::begin(ENGINE_SHADER_SYMBOLS),
				std::end(ENGINE_SHADER_SYMBOLS),
				[name](const ShaderSymbol& symbol) { return symbol.name == name; });

			return found == std::end(ENGINE_SHADER_SYMBOLS) ? nullptr : &*found;
		};

		// GLSL, with the engine's names coloured as GLSL's own are.
		const TextEditor::Language* Language()
		{
			static const TextEditor::Language language = []()
			{
				TextEditor::Language glsl = *TextEditor::Language::Glsl();

				for (const ShaderSymbol& symbol : ENGINE_SHADER_SYMBOLS)
					glsl.identifiers.emplace(symbol.name);

				return glsl;
			}();

			return &language;
		};
	};

	std::pair<size_t, size_t> ShaderEditor::MessageSpan(const std::string& line, const std::string& message)
	{
		const size_t open = message.find(quote);
		const size_t close = open == std::string::npos ? std::string::npos : message.find(quote, open + 1);

		if (close != std::string::npos && close > open + 1)
		{
			const std::string token = message.substr(open + 1, close - open - 1);

			// The token itself, not the same letters inside a longer name.
			for (size_t at = line.find(token); at != std::string::npos; at = line.find(token, at + 1))
			{
				const size_t end = at + token.size();

				if ((at == 0 || !IsIdentifier(line[at - 1])) && (end == line.size() || !IsIdentifier(line[end])))
					return { at, end };
			};
		};

		const size_t start = line.find_first_not_of(" \t");

		return start == std::string::npos ? std::pair<size_t, size_t>{ 0, line.size() } : std::pair<size_t, size_t>{ start, line.size() };
	};

	void ShaderEditor::Relocate(const std::string& from, const std::string& to)
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			document->path = ProjectAssets::Relocate(document->path, from, to);
	};

	void ShaderEditor::Open(const std::string& path)
	{
		// Absolute, since opening a project moves the working directory.
		std::error_code code;
		const std::string normal = std::filesystem::absolute(path, code).lexically_normal().string();

		m_focusFrames = focus_frames;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->path == normal)
			{
				document->select = true;
				return;
			};

		std::ifstream file(normal, std::ios::binary);

		if (!file)
		{
			std::cerr << "Shaders: could not open " << normal << std::endl;
			return;
		};

		std::unique_ptr<Document> document = std::make_unique<Document>();
		Document* const opened = document.get();

		opened->path = normal;

		TextEditor& editor = opened->editor;
		editor.SetLanguage(Language());
		editor.SetTabSize(tab_size);
		editor.SetShowWhitespacesEnabled(false);
		opened->savedText.assign(std::istreambuf_iterator<char>(file), { });
		editor.SetText(opened->savedText);
		editor.SetChangeCallback([opened]() { Check(*opened); }, check_delay_milliseconds);

		TextEditor::AutoCompleteConfig autoComplete;
		autoComplete.callback = [opened](TextEditor::AutoCompleteState& state) { Suggest(*opened, state); };
		editor.SetAutoCompleteConfig(&autoComplete);

		Check(*opened);
		editor.SetFocus();

		m_documents.push_back(std::move(document));
	};

	void ShaderEditor::Draw(bool* open)
	{
		if (m_focusFrames > 0)
		{
			m_focusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_focused = false;

		if (ImGui::Begin(EditorPanels::Shaders, open))
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (m_documents.empty())
				ImGui::TextDisabled("Double-click a shader in the Project panel to edit it here.");
			else if (ImGui::BeginTabBar("##shaders", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
			{
				const Document* closed = nullptr;

				for (const std::unique_ptr<Document>& document : m_documents)
				{
					ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;

					if (document->unsaved)
						flags |= ImGuiTabItemFlags_UnsavedDocument;

					if (std::exchange(document->select, false))
						flags |= ImGuiTabItemFlags_SetSelected;

					// Known by the document rather than its path, so a file moved
					// in the Project panel keeps its tab.
					const std::string label = std::string(EditorIcons::Shader) + " " + std::filesystem::path(document->path).filename().string() + "###" + std::to_string((uintptr_t)document.get());
					bool keep = true;
					const bool shown = ImGui::BeginTabItem(label.c_str(), &keep, flags);

					ImGui::SetItemTooltip("%s", document->path.c_str());

					if (shown)
					{
						DrawDocument(*document);
						ImGui::EndTabItem();
					};

					if (!keep)
					{
						if (document->unsaved)
							m_closing = document.get();
						else
							closed = document.get();
					};
				};

				ImGui::EndTabBar();
				Close(closed);
			};

			DrawCloseQuestion();
		};

		ImGui::End();
	};

	void ShaderEditor::DrawDocument(Document& document)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
			Save(document);

		const size_t rows = std::min(document.messages.size(), max_message_rows);
		const float messages_height = rows == 0 ? 0.0f : (float)rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

		ImGui::PushFont(EditorTheme::CodeFont(), 0.0f);
		// Against the text rather than the undo history, which an edit made
		// after an undo can bring back to where it was saved.
		if (document.editor.Render("##text", ImVec2(0.0f, -messages_height)))
			document.unsaved = document.editor.GetText() != document.savedText;
		ImGui::PopFont();

		Explain(document);

		DrawMessages(document);
	};

	void ShaderEditor::DrawMessages(Document& document)
	{
		if (document.messages.empty())
			return;

		if (ImGui::BeginChild("##messages"))
		{
			for (size_t i = 0; i < document.messages.size(); i++)
			{
				const ShaderMessage& message = document.messages[i];
				const std::string label = message.line == 0 ? message.text : "Line " + std::to_string(message.line) + ": " + message.text;

				ImGui::PushID((int)i);
				ImGui::PushStyleColor(ImGuiCol_Text, Colour(message));

				if (ImGui::Selectable(label.c_str()) && message.line != 0)
				{
					const size_t line = message.line - 1;

					document.editor.SetCursor(TextEditor::DocPos(line, 0));
					document.editor.ScrollToLine(line);
					document.editor.SetFocus();
				};

				ImGui::PopStyleColor();
				ImGui::PopID();
			};
		};

		ImGui::EndChild();
	};

	void ShaderEditor::DrawCloseQuestion()
	{
		constexpr const char* popup = "Unsaved Shader";

		if (m_closing != nullptr && !ImGui::IsPopupOpen(popup))
			ImGui::OpenPopup(popup);

		if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_closing == nullptr)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		};

		ImGui::Text("Save the changes to %s?", std::filesystem::path(m_closing->path).filename().string().c_str());

		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		// A save that fails leaves the question up, with the edits still open.
		const bool saved = save && Save(*m_closing);

		if (saved || discard)
			Close(m_closing);

		if (saved || discard || cancel)
		{
			m_closing = nullptr;
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndPopup();
	};

	void ShaderEditor::Close(const Document* document)
	{
		std::erase_if(m_documents, [document](const std::unique_ptr<Document>& open) { return open.get() == document; });
	};

	void ShaderEditor::Check(Document& document)
	{
		TextEditor& editor = document.editor;

		document.messages = Shader::Check(editor.GetText());

		editor.ClearMarkers();
		editor.ClearSquiggles(message_squiggles);

		// One marker a line, saying everything about it, worst first.
		std::map<size_t, std::vector<const ShaderMessage*>> by_line;

		for (const ShaderMessage& message : document.messages)
			if (message.line != 0 && message.line <= editor.GetLineCount())
				by_line[message.line - 1].push_back(&message);

		for (auto& [line, messages] : by_line)
		{
			std::stable_sort(
				messages.begin(),
				messages.end(),
				[](const ShaderMessage* a, const ShaderMessage* b) { return a->error && !b->error; });

			std::string tooltip;

			for (const ShaderMessage* message : messages)
				tooltip += (tooltip.empty() ? "" : "\n") + message->text;

			editor.AddMarker(line, Colour(*messages.front()), Colour(*messages.front(), marker_alpha), tooltip, tooltip);

			const std::string text = editor.GetLineText(line);

			for (const ShaderMessage* message : messages)
			{
				const auto [start, end] = MessageSpan(text, message->text);

				editor.AddSquiggle(
					TextEditor::DocPos(line, start),
					TextEditor::DocPos(line, end),
					message_squiggles,
					Colour(*message),
					message->text);
			};
		};
	};

	bool ShaderEditor::Save(Document& document)
	{
		std::ofstream file(document.path, std::ios::binary);
		std::string text = document.editor.GetText();

		if (!(file << text))
		{
			std::cerr << "Shaders: could not save " << document.path << std::endl;
			return false;
		};

		document.savedText = std::move(text);
		document.unsaved = false;

		return true;
	};

	void ShaderEditor::Suggest(const Document& document, TextEditor::AutoCompleteState& state)
	{
		std::set<std::string> words;

		const auto offer = [&words, &state](std::string_view word)
		{
			if (word.size() > state.searchTerm.size() && word.starts_with(state.searchTerm))
				words.emplace(word);
		};

		// The language has the engine's names among its identifiers.
		if (state.language != nullptr)
			for (const auto* known : { &state.language->keywords, &state.language->declarations, &state.language->identifiers })
				for (const std::string& word : *known)
					offer(word);

		document.editor.IterateIdentifiers(offer);

		state.suggestions.assign(words.begin(), words.end());
	};

	void ShaderEditor::Explain(const Document& document)
	{
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			return;

		const ShaderSymbol* symbol = EngineSymbol(document.editor.GetWordAtMousePos(ImGui::GetMousePos()));

		if (symbol == nullptr || !ImGui::BeginTooltip())
			return;

		ImGui::PushFont(EditorTheme::CodeFont(), 0.0f);
		ImGui::TextUnformatted(symbol->declaration.data(), symbol->declaration.data() + symbol->declaration.size());
		ImGui::PopFont();

		ImGui::PushTextWrapPos(ImGui::GetFontSize() * explanation_width);
		ImGui::TextUnformatted(symbol->description.data(), symbol->description.data() + symbol->description.size());
		ImGui::PopTextWrapPos();

		ImGui::EndTooltip();
	};
};

#endif

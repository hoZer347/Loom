#include "TextFileEditor.h"

// Not in the web test build, which has no TextEditor.
#ifndef __EMSCRIPTEN__

#include "EditorTheme.h"
#include "ProjectAssets.h"

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
		#pragma region Constants

		// After the last keystroke, before the text is checked.
		constexpr int check_delay_milliseconds = 300;

		constexpr size_t tab_size = 4;

		// The squiggle type the check's messages are drawn with.
		constexpr size_t message_squiggles = 1;

		// A window docked on its first frame only takes its tab on the next.
		constexpr int focus_frames = 2;

		// How strongly a line with a message on it is tinted.
		constexpr float marker_alpha = 0.15f;

		// The most messages listed under the text before the list scrolls.
		constexpr size_t max_message_rows = 6;

		constexpr char quote = '\'';

		#pragma endregion

		#pragma region Helpers

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

		#pragma endregion
	};

	#pragma region Documents

	void TextFileEditor::Open(const std::string& path)
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
			std::cerr << "Could not open " << normal << std::endl;
			return;
		};

		std::unique_ptr<Document> document = std::make_unique<Document>();
		Document* const opened = document.get();

		opened->path = normal;

		TextEditor& editor = opened->editor;
		editor.SetLanguage(Language());
		editor.SetTabSize(tab_size);
		editor.SetShowWhitespacesEnabled(false);
		editor.SetText(std::string(std::istreambuf_iterator<char>(file), { }));

		// As the editor holds it, without the file's carriage returns.
		opened->savedText = editor.GetText();
		editor.SetChangeCallback([this, opened]() { Mark(*opened); }, check_delay_milliseconds);

		TextEditor::AutoCompleteConfig autoComplete;
		autoComplete.callback = [this, opened](TextEditor::AutoCompleteState& state) { Suggest(*opened, state); };
		editor.SetAutoCompleteConfig(&autoComplete);

		Mark(*opened);
		editor.SetFocus();

		m_documents.push_back(std::move(document));
	};

	void TextFileEditor::Relocate(const std::string& from, const std::string& to)
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			document->path = ProjectAssets::Relocate(document->path, from, to);
	};

	bool TextFileEditor::HasUnsaved(const std::string& path) const
	{
		return std::ranges::any_of(m_documents, [&path](const std::unique_ptr<Document>& document)
		{
			return document->unsaved && ProjectAssets::IsUnder(document->path, path);
		});
	};

	void TextFileEditor::CloseUnder(const std::string& path)
	{
		std::erase_if(m_documents, [&path](const std::unique_ptr<Document>& document)
		{
			return ProjectAssets::IsUnder(document->path, path);
		});
	};

	bool TextFileEditor::SaveAll()
	{
		bool saved = true;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->unsaved)
				saved = Save(*document) && saved;

		return saved;
	};

	std::vector<std::string> TextFileEditor::UnsavedFiles() const
	{
		std::vector<std::string> unsaved{ };

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->unsaved)
				unsaved.push_back(std::filesystem::path(document->path).filename().string());

		return unsaved;
	};

	bool TextFileEditor::Save(Document& document)
	{
		std::ofstream file(document.path, std::ios::binary);
		std::string text = document.editor.GetText();

		if (!(file << text))
		{
			std::cerr << "Could not save " << document.path << std::endl;
			return false;
		};

		document.savedText = std::move(text);
		document.unsaved = false;

		return true;
	};

	void TextFileEditor::Close(const Document* document)
	{
		std::erase_if(m_documents, [document](const std::unique_ptr<Document>& open) { return open.get() == document; });
	};

	#pragma endregion

	#pragma region Drawing

	void TextFileEditor::Draw(bool* open)
	{
		if (m_focusFrames > 0)
		{
			m_focusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_focused = false;

		if (ImGui::Begin(Panel(), open))
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (m_documents.empty())
				ImGui::TextDisabled("%s", Hint());
			else if (ImGui::BeginTabBar("##files", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
			{
				const Document* closed = nullptr;

				for (const std::unique_ptr<Document>& document : m_documents)
				{
					ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;

					if (document->unsaved)
						flags |= ImGuiTabItemFlags_UnsavedDocument;

					if (std::exchange(document->select, false))
						flags |= ImGuiTabItemFlags_SetSelected;

					// By document rather than path, so a moved file keeps its tab.
					const std::string label = std::string(Icon()) + " " + std::filesystem::path(document->path).filename().string() + "###" + std::to_string((uintptr_t)document.get());
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

	void TextFileEditor::DrawDocument(Document& document)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
			Save(document);

		// For the rest of the panel, as the text takes these itself while it has the keyboard.
		const size_t undo_index = document.editor.GetUndoIndex();

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z))
			document.editor.Undo();
		else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
			document.editor.Redo();

		const size_t rows = std::min(document.messages.size(), max_message_rows);
		const float messages_height = rows == 0 ? 0.0f : (float)rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

		ImGui::PushFont(EditorTheme::CodeFont(), 0.0f);
		// Against the text, since an edit after an undo can return to the saved state.
		if (document.editor.Render("##text", ImVec2(0.0f, -messages_height)) || undo_index != document.editor.GetUndoIndex())
			document.unsaved = document.editor.GetText() != document.savedText;
		ImGui::PopFont();

		Explain(document);

		DrawMessages(document);
	};

	void TextFileEditor::DrawMessages(Document& document)
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

	void TextFileEditor::DrawCloseQuestion()
	{
		constexpr const char* popup = "Unsaved Changes";

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

	#pragma endregion

	#pragma region Language

	std::pair<size_t, size_t> TextFileEditor::MessageSpan(const std::string& line, const std::string& message)
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

	void TextFileEditor::Mark(Document& document) const
	{
		TextEditor& editor = document.editor;

		document.messages = Check(document);

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

	void TextFileEditor::Suggest(const Document& document, TextEditor::AutoCompleteState& state) const
	{
		std::set<std::string> words;

		const auto offer = [&words, &state](std::string_view word)
		{
			if (word.size() > state.searchTerm.size() && word.starts_with(state.searchTerm))
				words.emplace(word);
		};

		if (state.language != nullptr)
			for (const auto* known : { &state.language->keywords, &state.language->declarations, &state.language->identifiers })
				for (const std::string& word : *known)
					offer(word);

		document.editor.IterateIdentifiers(offer);

		state.suggestions.assign(words.begin(), words.end());
	};

	#pragma endregion
};

#endif

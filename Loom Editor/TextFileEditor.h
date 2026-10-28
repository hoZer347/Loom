#pragma once

#include "Shaders.h"

#include "TextEditor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::TextFileEditor
	* - A panel with a tab for each open file of one kind, highlighted, completed
	*   and checked as the kind says
	* - Checks the text a moment after each edit and marks what the check says on
	*   the line it is about. Ctrl+S saves
	*/
	struct TextFileEditor
	{
		virtual ~TextFileEditor() = default;

		#pragma region Documents

		// Opens the file in a tab of its own, or brings its tab forward, and
		// the panel with it.
		void Open(const std::string& path);

		// Keeps the tabs of files that were moved saving to where they are now.
		void Relocate(const std::string& from, const std::string& to);

		// Every tab with unsaved changes, to its file. False when one could not
		// be written.
		bool SaveAll();

		// The names of the files with unsaved changes.
		std::vector<std::string> UnsavedFiles() const;

		#pragma endregion

		#pragma region Drawing

		void Draw(bool* open);

		// Whether the panel had the keyboard when it was last drawn, so Ctrl+S
		// saves its file rather than the scene.
		bool IsFocused() const { return m_focused; };

		#pragma endregion

		#pragma region Language

		// Where on the line to underline a message about it: what the message
		// quotes, as in "'x' : undeclared identifier", when the line has it as
		// a word of its own, and otherwise the line from its first non-blank.
		static std::pair<size_t, size_t> MessageSpan(const std::string& line, const std::string& message);

		#pragma endregion

	protected:
		struct Document
		{
			std::string path;
			TextEditor editor;
			std::string savedText;
			bool unsaved = false;
			std::vector<ShaderMessage> messages;

			// Brings the tab forward on the next frame.
			bool select = true;
		};

		#pragma region Kind

		// The panel's window title.
		virtual const char* Panel() const = 0;

		virtual const char* Icon() const = 0;

		// Shown in the panel while no file is open in it.
		virtual const char* Hint() const = 0;

		virtual const TextEditor::Language* Language() const = 0;

		// What is wrong with the document's text, by line.
		virtual std::vector<ShaderMessage> Check(const Document& document) const = 0;

		// The language's words and the file's own.
		virtual void Suggest(const Document& document, TextEditor::AutoCompleteState& state) const;

		// A tooltip for what is under the mouse, when there is one.
		virtual void Explain(const Document& document) const { };

		#pragma endregion

	private:
		#pragma region Documents

		static bool Save(Document& document);
		void Close(const Document* document);

		#pragma endregion

		#pragma region Drawing

		void DrawDocument(Document& document);
		void DrawMessages(Document& document);
		void DrawCloseQuestion();

		#pragma endregion

		#pragma region Language

		// Checks the document and marks what is wrong on its lines.
		void Mark(Document& document) const;

		#pragma endregion

		// Each owns the TextEditor its callbacks point back at, so a document
		// never moves.
		std::vector<std::unique_ptr<Document>> m_documents;

		// Closed with edits that were never saved, until the user says what to
		// do with them.
		Document* m_closing = nullptr;

		// Frames left to ask for focus on.
		int m_focusFrames = 0;
		bool m_focused = false;
	};
};

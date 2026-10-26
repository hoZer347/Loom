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
	* Loom::ShaderEditor
	* - The Shaders panel: a tab for each open .shader file, highlighted as
	*   GLSL, completing GLSL's words, the file's own and the engine's, and
	*   explaining the engine's on hover
	* - Checks the text with glslang a moment after each edit and marks what it
	*   says on the line it is about. Ctrl+S saves, and the engine reloads the
	*   shader from the file
	*/
	struct ShaderEditor final
	{
		#pragma region Documents

		// Opens the file in a tab of its own, or brings its tab forward, and
		// the panel with it.
		void Open(const std::string& path);

		// Keeps the tabs of files that were moved saving to where they are now.
		void Relocate(const std::string& from, const std::string& to);

		// Every tab with unsaved changes, to its file. False when one could not
		// be written.
		bool SaveAll();

		#pragma endregion

		#pragma region Drawing

		void Draw(bool* open);

		// Whether the panel had the keyboard when it was last drawn, so Ctrl+S
		// saves its shader rather than the scene.
		bool IsFocused() const { return m_focused; };

		#pragma endregion

		#pragma region Language

		// Where on the line to underline a message about it: what glslang
		// quotes, as in "'x' : undeclared identifier", when the line has it as
		// a word of its own, and otherwise the line from its first non-blank.
		static std::pair<size_t, size_t> MessageSpan(const std::string& line, const std::string& message);

		#pragma endregion

	private:
		#pragma region Documents

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

		static bool Save(Document& document);
		void Close(const Document* document);

		#pragma endregion

		#pragma region Drawing

		void DrawDocument(Document& document);
		void DrawMessages(Document& document);
		void DrawCloseQuestion();

		#pragma endregion

		#pragma region Language

		static void Check(Document& document);
		static void Suggest(const Document& document, TextEditor::AutoCompleteState& state);

		// A tooltip for the engine name under the mouse, when there is one.
		static void Explain(const Document& document);

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

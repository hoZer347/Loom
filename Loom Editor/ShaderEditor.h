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
		static constexpr const char* window = "Shaders";

		// Opens the file in a tab of its own, or brings its tab forward, and
		// the panel with it.
		void Open(const std::string& path);

		void Draw(bool* open);

		// Where on the line to underline a message about it: what glslang
		// quotes, as in "'x' : undeclared identifier", when the line has it as
		// a word of its own, and otherwise the line from its first non-blank.
		static std::pair<size_t, size_t> MessageSpan(const std::string& line, const std::string& message);

	private:
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

		static void Check(Document& document);
		static bool Save(Document& document);
		static void Suggest(const Document& document, TextEditor::AutoCompleteState& state);

		void DrawDocument(Document& document);
		void DrawMessages(Document& document);
		void DrawCloseQuestion();

		// A tooltip for the engine name under the mouse, when there is one.
		static void Explain(const Document& document);
		void Close(const Document* document);

		// Each owns the TextEditor its callbacks point back at, so a document
		// never moves.
		std::vector<std::unique_ptr<Document>> m_documents;

		// Closed with edits that were never saved, until the user says what to
		// do with them.
		Document* m_closing = nullptr;

		// Frames left to ask for focus on.
		int m_focusFrames = 0;
	};
};

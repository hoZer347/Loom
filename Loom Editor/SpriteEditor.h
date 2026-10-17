#pragma once

#include "imgui.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::SpriteEditor
	* - The Sprites panel: a tab for each open image, drawn pixel for pixel
	*   over a checkerboard where it is see-through
	* - The left mouse paints with the pencil, eraser, fill or picker; the
	*   middle mouse pans and the wheel zooms about the mouse. Ctrl+Z and
	*   Ctrl+Y step back and forward a stroke at a time
	* - Ctrl+S writes the file in the format its extension names, and every
	*   Sprite showing it picks the change up
	*/
	struct SpriteEditor final
	{
		using Colour = std::array<uint8_t, 4>;

		// Eight bit RGBA, top row first.
		struct Image
		{
			int width = 0;
			int height = 0;
			std::vector<uint8_t> pixels;

			bool Contains(int x, int y) const;
			Colour Get(int x, int y) const;

			// Off the image does nothing.
			void Set(int x, int y, const Colour& colour);

			// Every pixel from one end to the other, both included, so a stroke
			// has no gaps however fast the mouse went.
			void Line(int x0, int y0, int x1, int y1, const Colour& colour);

			// The pixel and every one joined to it side by side through pixels
			// of its colour.
			void Fill(int x, int y, const Colour& colour);

			bool operator==(const Image&) const = default;
		};

		enum struct Tool
		{
			Pencil,
			Eraser,
			Fill,
			Picker,
		};

		// Each says what went wrong on std::cerr.
		static bool Load(const std::string& path, Image& image);
		static bool Save(const std::string& path, const Image& image);

		// The file Save writes for an extension, such as ".png". Empty for one
		// it cannot write.
		static std::vector<char> Encode(const std::string& extension, const Image& image);

		// Opens the file in a tab of its own, or brings its tab forward, and
		// the panel with it.
		void Open(const std::string& path);

		void Draw(bool* open);

		// Keeps the tabs of files that were moved saving to where they are now.
		void Relocate(const std::string& from, const std::string& to);

		// Whether the panel had the keyboard when it was last drawn, so the
		// scene's own undo keeps out of its way.
		bool IsFocused() const { return m_focused; };

		// The files saved since the last call.
		std::vector<std::string> TakeSaved();

	private:
		struct Document
		{
			std::string path;
			Image image;
			Image saved;
			bool unsaved = false;

			// One image for each stroke, as it was before it.
			std::vector<Image> undo;
			std::vector<Image> redo;

			// Screen pixels to an image pixel, and where the image's top left
			// sits in the canvas. Zero until the canvas first has a size.
			float zoom = 0.0f;
			ImVec2 offset{ };

			// The pixel a stroke last reached, while the mouse is held down.
			bool stroking = false;
			int lastX = 0;
			int lastY = 0;

			// Brings the tab forward on the next frame.
			bool select = true;
		};

		bool SaveDocument(Document& document);

		void DrawDocument(Document& document);
		void DrawToolbar(Document& document);
		void DrawCanvas(Document& document, const ImVec2& size);
		void DrawCloseQuestion();

		void Paint(Document& document, int x, int y, bool pressed);
		static void Remember(Document& document);
		static void Step(Document& document, bool undo);
		static void Fit(Document& document, const ImVec2& size);
		static void Zoom(Document& document, const ImVec2& about, float wheel);

		void Close(const Document* document);

		std::vector<std::unique_ptr<Document>> m_documents;

		// Closed with edits that were never saved, until the user says what to
		// do with them.
		Document* m_closing = nullptr;

		std::vector<std::string> m_saved;

		Tool m_tool = Tool::Pencil;
		Colour m_colour{ 0, 0, 0, UINT8_MAX };
		bool m_grid = true;
		bool m_focused = false;

		// Frames left to ask for focus on.
		int m_focusFrames = 0;
	};
};

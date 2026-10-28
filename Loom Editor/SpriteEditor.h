#pragma once

#include "SpriteAsset.h"

#include "imgui.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::SpriteEditor
	* - The Sprites panel: a tab for each open sprite, its frame drawn pixel
	*   for pixel over a checkerboard where it is see-through, over a timeline
	*   of its layers and frames
	* - The Palette panel: a bar of the pencil, eraser, fill and picker as
	*   icons, a colour for each of the left and right mouse buttons, which X
	*   swaps, and an import of the sprite's colours as a category; under it
	*   the swatches of the palette the sprite in front paints from, clear
	*   always first, a click on one picking it for the button that clicked.
	*   A sprite whose Palette panel is out of sight says so, with a button
	*   to bring it, and the Sprites panel opening with it closed asks
	*   whether to open it, docked in the bottom quarter of the Sprites panel
	* - Right-clicking the Palette panel adds a category, a header that opens
	*   and shuts on swatches of its own, which right-clicking renames or
	*   deletes, or opens another of the project's palettes
	* - The left and right mouse paint the layer and frame picked in the
	*   timeline, each with its own colour, and the picker picks for the
	*   button that clicked; the middle mouse pans and the wheel zooms about
	*   the mouse. Ctrl+Z and Ctrl+Y step back and forward an edit at a time,
	*   Resize included, which scales the pixels or cuts and grows round them
	* - A layer can be outlined, hidden or not, which draws round its pixels
	*   on the canvas and nowhere else, and dragged by its grip up or down the
	*   timeline, the others making way as it goes
	* - Ctrl+S writes the sprite, the image it is flattened into and its
	*   palette, and every Sprite showing that image picks the change up.
	*   Nothing is written before then
	* - The Palette panel keeps its own Ctrl+Z, Ctrl+Y and Ctrl+S, for the
	*   palette it shows
	*/
	struct SpriteEditor final
	{
		// The left and right mouse buttons, which each paint with a colour.
		static constexpr size_t paintButtons = 2;

		enum struct Tool
		{
			Pencil,
			Eraser,
			Fill,
			Picker,
		};

		#pragma region Opening

		// Opens a sprite, or the sprite kept for an image, in a tab of its own,
		// or brings its tab forward, and the panel with it. An image with no
		// sprite yet opens as one of a frame and a layer, written beside it on
		// the first save. A preview takes the place of the one before it, until
		// an edit or a double click on its tab keeps it; opening it again
		// without preview keeps it too.
		void Open(const std::string& path, bool preview = false);

		// Into the Palette panel, as the palette of the sprite in front, or on
		// its own when there is none.
		void OpenPalette(const std::string& path);

		// Keeps the tabs of files that were moved saving to where they are now.
		void Relocate(const std::string& from, const std::string& to);

		// The images saved since the last call.
		std::vector<std::string> TakeSaved();

		// The palettes renamed since the last call, from where to where.
		std::vector<std::pair<std::string, std::string>> TakeRenamed();

		#pragma endregion

		#pragma region Windows

		// palette_open is the Palette panel's, which a sprite's tab can open.
		void Draw(bool* open, bool* palette_open);

		// Whether each panel had the keyboard when it was last drawn, so the
		// scene's own undo keeps out of its way.
		bool IsFocused() const { return m_focused; };
		bool IsPaletteFocused() const { return m_paletteFocused; };

		#pragma endregion

		#pragma region Palette Window

		// project_folder is where it looks for palettes to pick from, and for
		// sprites to point at one renamed, or beside the sprite when it is
		// empty.
		void DrawPalette(bool* open, const std::string& project_folder);

		#pragma endregion

		#pragma region Saving

		// Every tab with unsaved changes, to its files. False when one could
		// not be written.
		bool SaveAll();

		#pragma endregion

	private:
		#pragma region Document

		using Image = SpriteAsset::Image;
		using Colour = SpriteAsset::Colour;

		struct Snapshot
		{
			SpriteAsset sprite;
			SpriteAsset::Links links;
		};

		struct Document
		{
			// The .loomsprite, which may not be written yet.
			std::string path;
			SpriteAsset::Links links;
			SpriteAsset sprite;

			SpriteAsset saved;
			SpriteAsset::Links savedLinks;
			bool unsaved = false;

			// The image as it was found, when something else wrote it since the
			// sprite was saved, until the user takes it in or keeps the sprite.
			std::optional<Image> outside;

			// The sprite and its links as they were before each edit.
			std::vector<Snapshot> undo;
			std::vector<Snapshot> redo;

			// What is painted, as the timeline picked it.
			int layer = 0;
			int frame = 0;

			// The frame's visible layers together, when there is more than one,
			// made again once stale.
			Image flattened;
			bool stale = true;

			// Milliseconds into the frame, while it plays.
			bool playing = false;
			float played = 0.0f;

			// Screen pixels to an image pixel, and where the image's top left
			// sits in the canvas. Zero until the canvas first has a size.
			float zoom = 0.0f;
			ImVec2 offset{ };

			// The pixel under the mouse, for the status line.
			bool hovering = false;
			int hoverX = 0;
			int hoverY = 0;

			// The pixel a stroke last reached, and the button painting it, while
			// it is held down.
			bool stroking = false;
			ImGuiMouseButton strokeButton = ImGuiMouseButton_Left;
			int lastX = 0;
			int lastY = 0;

			// The layer held by its grip in the timeline, which follows the
			// mouse up and down the rows until it lets go, or -1. Whether it has
			// moved yet, so one hold is one step to undo.
			int grabbed = -1;
			bool grabMoved = false;

			// Brings the tab forward on the next frame.
			bool select = true;

			// Shown in italics, and replaced by the next preview opened.
			bool preview = false;
		};

		#pragma endregion

		#pragma region Windows

		void DrawDocument(Document& document, bool* palette_open, bool palette_shown);

		#pragma endregion

		#pragma region Canvas

		void DrawCanvas(Document& document, const ImVec2& size);

		#pragma endregion

		#pragma region Timeline

		void DrawTimeline(Document& document, float height);

		#pragma endregion

		#pragma region Status

		void DrawStatus(Document& document, bool* palette_open, bool palette_shown);
		void DrawResize(Document& document);

		#pragma endregion

		#pragma region Palette Question

		// Asked as the Sprites panel opens with the Palette panel closed. dock is
		// the Sprites panel's node, which the Palette panel is docked under.
		void DrawPaletteQuestion(bool* palette_open, ImGuiID dock);

		// Docks the Palette panel under the Sprites panel's node, once asked to.
		void DockPalette();

		#pragma endregion

		#pragma region Palette Window

		void DrawTools();

		// Asked before an import of many colours, each a swatch of its own.
		void DrawImportQuestion();
		void Import(const std::string& path, Palette::Category category);
		void DrawSwatches(const std::string& path);
		// Clear is first among the palette's own colours, and in no category.
		void DrawSection(const std::string& path, std::vector<Colour>& colours, bool with_clear, const std::string& add_label);
		void DrawCategories(const std::string& path);
		void DrawPaletteChoice(const std::string& project_folder);
		void DrawPaletteMenu(const std::string& project_folder);
		void DrawPaletteRename(const std::string& project_folder);

		// Into m_paletteFiles, from the project, or beside the sprite, and those made and not yet saved.
		void FindPalettes(const std::string& project_folder);

		#pragma endregion

		#pragma region Tool Shortcuts

		// The tools' keys, and X to swap the colours.
		void ToolShortcuts();

		#pragma endregion

		#pragma region Palettes

		struct PaletteFile
		{
			Palette palette;

			// As last written, or nothing for one made since.
			std::optional<Palette> saved;

			// The palette as it was before each edit.
			std::vector<Palette> undo;
			std::vector<Palette> redo;

			bool Unsaved() const { return !saved || palette != *saved; };
		};

		// The palette of the sprite in front, or the one opened on its own.
		const std::string& PalettePath() const;
		void SetPalette(const std::string& path);
		PaletteFile& PaletteAt(const std::string& path);

		static void RememberPalette(PaletteFile& file);
		// False when there is nothing that way.
		static bool StepPalette(PaletteFile& file, bool undo);
		bool SavePalette(const std::string& path);

		#pragma endregion

		#pragma region Painting

		// What the frame looks like.
		static const Image& Shown(Document& document);

		// pressed is the button that went down on the canvas this frame.
		void Paint(Document& document, int x, int y, std::optional<ImGuiMouseButton> pressed);

		#pragma endregion

		#pragma region Playback

		void Play(Document& document);

		#pragma endregion

		#pragma region History

		static void Remember(Document& document);

		// Ends an edit begun by Remember, leaving nothing to undo when it
		// changed nothing.
		static void Settle(Document& document);

		static void Edited(Document& document);
		static void Step(Document& document, bool undo);

		#pragma endregion

		#pragma region View

		static void Fit(Document& document, const ImVec2& size);
		static void Zoom(Document& document, const ImVec2& about, float wheel);

		#pragma endregion

		#pragma region Saving

		bool SaveDocument(Document& document);

		#pragma endregion

		#pragma region Closing

		void DrawCloseQuestion();

		void Close(const Document* document);

		#pragma endregion

		#pragma region Documents

		std::vector<std::unique_ptr<Document>> m_documents;

		// The tab last in front of the Sprites panel, whose palette the Palette
		// panel shows.
		Document* m_active = nullptr;

		// Closed with edits that were never saved, until the user says what to
		// do with them.
		Document* m_closing = nullptr;

		std::vector<std::string> m_saved;

		#pragma endregion

		#pragma region Editing

		Tool m_tool = Tool::Pencil;
		// What the left and right mouse buttons paint with, by ImGuiMouseButton.
		std::array<Colour, paintButtons> m_colours{ Colour{ 0, 0, 0, UINT8_MAX }, Colour{ UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX } };
		bool m_grid = true;

		// What a layer's or a category's rename box holds while it is open.
		static constexpr size_t nameSize = 64;
		char m_layerName[nameSize] = { };
		char m_categoryName[nameSize] = { };

		// What the Resize popup holds while it is open. The anchor counts
		// across its grid from the top left.
		int m_resizeWidth = 0;
		int m_resizeHeight = 0;
		bool m_resizeScale = true;
		int m_resizeAnchor = 0;

		// An import waiting on its question, and the palette it goes into.
		std::optional<Palette::Category> m_importing;
		std::string m_importPath;

		#pragma endregion

		#pragma region Palettes

		// Every palette shown or made so far, by path.
		std::map<std::string, PaletteFile> m_palettes;

		// Opened with no sprite in front to take it.
		std::string m_loosePalette;

		// Found when the list of them opens.
		std::vector<std::string> m_paletteFiles;

		// The category just added, whose rename box opens on the next frame,
		// or -1.
		int m_renameCategory = -1;

		// What the palette's rename box holds while it is open.
		static constexpr size_t paletteNameSize = 256;
		char m_paletteName[paletteNameSize] = { };

		// Palettes renamed since TakeRenamed last ran, from where to where.
		std::vector<std::pair<std::string, std::string>> m_renamed;

		#pragma endregion

		#pragma region Focus

		bool m_focused = false;

		// Whether the Palette panel was in sight when it was last drawn.
		bool m_paletteShown = false;
		bool m_paletteFocused = false;

		// Frames left to ask for focus on.
		int m_focusFrames = 0;
		int m_paletteFocusFrames = 0;

		bool m_askPalette = false;

		// The Sprites panel's dock node, to split for the Palette panel on the next frame.
		ImGuiID m_paletteDock = 0;

		#pragma endregion
	};
};

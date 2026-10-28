#include "SpriteEditor.h"

#include "EditorIcons.h"
#include "ProjectAssets.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Constants

		constexpr int channels = 4;
		constexpr int alpha = 3;

		// A window docked on the frame it first appears only takes its tab on
		// the one after.
		constexpr int focus_frames = 2;

		// How much of the Sprites panel's height a Palette panel docked under it takes.
		constexpr float palette_share = 0.25f;

		// Screen pixels to an image pixel. From one up, zoom goes in whole
		// steps, so every image pixel is the same size on screen.
		constexpr float min_zoom = 1.0f / 16.0f;
		constexpr float max_zoom = 64.0f;
		constexpr float wheel_zoom = 1.25f;

		// How much of the canvas a fitted image takes.
		constexpr float fit_share = 0.9f;

		// The pixel grid shows from this zoom up.
		constexpr float grid_zoom = 8.0f;

		// The most pixels drawn across or down. A larger image zoomed out is
		// drawn a block of pixels to a cell, coloured by its top left one.
		constexpr int max_cells = 256;

		// In screen pixels, behind the image where it is see-through.
		constexpr float checker_size = 8.0f;
		constexpr ImU32 checker_light = IM_COL32(204, 204, 204, 255);
		constexpr ImU32 checker_dark = IM_COL32(153, 153, 153, 255);

		constexpr ImU32 grid_colour = IM_COL32(0, 0, 0, 64);
		constexpr ImU32 border_colour = IM_COL32(0, 0, 0, 160);
		constexpr ImU32 hover_colour = IM_COL32(255, 255, 255, 200);

		// Round the pixels of an outlined layer, in screen pixels.
		constexpr ImU32 outline_colour = IM_COL32(0, 255, 255, 255);
		constexpr float outline_thickness = 2.0f;

		// Shared by a document's edits, so a large sprite keeps fewer of them.
		constexpr size_t max_undo_bytes = 256 * 1024 * 1024;

		// The timeline's rows of layers, beyond which it scrolls, and in lines
		// of text the widths of a frame's milliseconds, of its layers column
		// and of a frame's column.
		constexpr int timeline_rows = 4;
		constexpr float name_lines = 8.0f;
		constexpr float layers_lines = 12.0f;
		constexpr float frame_lines = 1.5f;

		// The timeline is a table, with a column for the layers' names and
		// one for each frame.
		static_assert(SpriteAsset::maxFrames < IMGUI_TABLE_MAX_COLUMNS);

		// Above the layers: the timeline's buttons, and the frames' numbers.
		constexpr float timeline_head_rows = 2.0f;

		constexpr float milliseconds = 1000.0f;

		// How fast the timeline scrolls while a layer is held past its top or
		// bottom row.
		constexpr float held_scroll_rows = 8.0f;

		// The dots on the square a layer is dragged by, as braille's six, and
		// their radius as a share of the line.
		constexpr int grip_columns = 2;
		constexpr int grip_rows = 3;
		constexpr float grip_dot = 0.1f;

		constexpr float half = 0.5f;

		// A new palette starts with the sprite's own colours, as many as fit.
		constexpr size_t seeded_colours = 64;

		// Swatches, in lines of text, and the corners marking those the left
		// and right mouse paint with, as a share of a swatch's side.
		constexpr float swatch_lines = 1.75f;
		constexpr float marker_share = 0.4f;

		// What a new category is called, numbered from the second on.
		constexpr const char* new_category = "Category";
		constexpr int first_numbered = 2;

		constexpr ImGuiKey swap_key = ImGuiKey_X;

		// How far a preview's tab name leans, across per line down.
		constexpr float italic_slant = 0.2f;

		// The Resize popup's anchors, a grid of them with the middle first
		// picked.
		constexpr const char* resize_popup = "Resize";
		constexpr int anchor_columns = 3;
		constexpr int middle_anchor = anchor_columns * anchor_columns / 2;

		// Wide enough, in lines of text, for the longest side a sprite can have.
		constexpr float resize_field_lines = 3.5f;

		#pragma endregion

		#pragma region Colours

		// First in every palette, whether its file has it or not, and painting
		// as the eraser does.
		constexpr SpriteAsset::Colour clear{ };

		// Any colour with no alpha paints as clear does, and is the clear
		// swatch, whatever its red, green and blue.
		bool SeeThrough(const SpriteAsset::Colour& colour)
		{
			return colour[alpha] == 0;
		};

		// What a colour writes into the image, clear for one that is see-through.
		SpriteAsset::Colour Painted(const SpriteAsset::Colour& colour)
		{
			return SeeThrough(colour) ? clear : colour;
		};

		bool Matches(const SpriteAsset::Colour& swatch, const SpriteAsset::Colour& chosen)
		{
			return swatch == chosen || (SeeThrough(swatch) && SeeThrough(chosen));
		};

		#pragma endregion

		#pragma region Tools

		constexpr ImGuiMouseButton paint_buttons[] = { ImGuiMouseButton_Left, ImGuiMouseButton_Right };
		static_assert(std::size(paint_buttons) == SpriteEditor::paintButtons);
		constexpr const char* button_names[] = { "Left", "Right" };

		struct ToolInfo
		{
			SpriteEditor::Tool tool;
			const char* icon;
			const char* label;
			ImGuiKey key;
		};

		constexpr ToolInfo tools[] =
		{
			{ SpriteEditor::Tool::Pencil, "✏", "Pencil", ImGuiKey_B },
			{ SpriteEditor::Tool::Eraser, "🧽", "Eraser", ImGuiKey_E },
			{ SpriteEditor::Tool::Fill, "🪣", "Fill", ImGuiKey_G },
			{ SpriteEditor::Tool::Picker, "💧", "Picker", ImGuiKey_I },
		};

		constexpr const char* import_icon = "📥";

		#pragma endregion

		#pragma region Helpers

		ImU32 ToImGui(const SpriteAsset::Colour& colour)
		{
			return IM_COL32(colour[0], colour[1], colour[2], colour[3]);
		};

		ImVec4 ToFloats(const SpriteAsset::Colour& colour)
		{
			return ImGui::ColorConvertU32ToFloat4(ToImGui(colour));
		};

		// Without padding, as a glyph wider than the padded inside starts at its left.
		bool IconButton(const char* icon, const char* id, float side)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
			const bool clicked = ImGui::Button((std::string(icon) + "##" + id).c_str(), ImVec2(side, side));
			ImGui::PopStyleVar();

			return clicked;
		};

		// Frames use the atlas's white pixel, so only glyphs lean.
		void Italicise(int first_vertex, float right)
		{
			ImDrawList& draw_list = *ImGui::GetWindowDrawList();
			const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
			const float middle = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * half;

			for (int i = first_vertex; i < draw_list.VtxBuffer.Size; i++)
			{
				ImDrawVert& vertex = draw_list.VtxBuffer[i];

				if (vertex.pos.x < right && (vertex.uv.x != white.x || vertex.uv.y != white.y))
					vertex.pos.x += (middle - vertex.pos.y) * italic_slant;
			};
		};

		std::string FileName(const std::string& path)
		{
			return std::filesystem::path(path).filename().string();
		};

		// Beside the sprite, under its stem, or with the first number after it
		// that nothing there, nor any palette made and not yet saved, is using.
		template <typename Taken>
		std::string NewPalettePath(const std::string& sprite, const Taken& taken)
		{
			std::filesystem::path path = std::filesystem::path(sprite).replace_extension(Palette::extension);
			const std::string stem = path.stem().string();

			std::error_code code;

			for (int suffix = 1; std::filesystem::exists(path, code) || taken(path.string()); suffix++)
				path.replace_filename(stem + std::to_string(suffix) + Palette::extension);

			return path.string();
		};

		#pragma endregion
	};

	#pragma region Opening

	void SpriteEditor::Open(const std::string& path, bool preview)
	{
		// Absolute, since opening a project moves the working directory.
		std::error_code code;
		const std::string normal = std::filesystem::absolute(path, code).lexically_normal().string();

		const bool sprite_file = std::filesystem::path(normal).extension() == SpriteAsset::extension;
		const std::string sprite_path = sprite_file ? normal : SpriteAsset::For(normal);

		m_focusFrames = focus_frames;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->path == sprite_path || document->links.image == normal)
			{
				document->select = true;
				document->preview = document->preview && preview;
				return;
			};

		std::unique_ptr<Document> document = std::make_unique<Document>();
		document->path = sprite_path;

		if (sprite_file || std::filesystem::exists(sprite_path, code))
		{
			// A sprite left beside it by another image is that image's.
			if (!sprite_file && SpriteAsset::ImageOf(sprite_path) != normal)
			{
				std::cerr << "Sprites: " << sprite_path << " is the sprite of another image, not " << normal << std::endl;
				return;
			};

			if (!SpriteAsset::Load(sprite_path, document->sprite, document->links))
				return;

			Image image;

			if (std::filesystem::exists(document->links.image, code) &&
				SpriteAsset::LoadImage(document->links.image, image) &&
				image != SpriteAsset::AsSaved(document->links.image, document->sprite.Sheet()))
				document->outside = std::move(image);
		}
		else
		{
			Image image;

			if (!SpriteAsset::LoadImage(normal, image))
				return;

			document->sprite = SpriteAsset::FromImage(std::move(image));
			document->links.image = normal;
		};

		document->saved = document->sprite;
		document->savedLinks = document->links;
		document->preview = preview;

		const auto previewed = std::find_if(m_documents.begin(), m_documents.end(),
			[](const std::unique_ptr<Document>& open) { return open->preview; });

		if (!preview || previewed == m_documents.end())
		{
			m_documents.push_back(std::move(document));
			return;
		};

		if (m_active == previewed->get())
			m_active = nullptr;

		*previewed = std::move(document);
	};

	void SpriteEditor::OpenPalette(const std::string& path)
	{
		std::error_code code;

		SetPalette(std::filesystem::absolute(path, code).lexically_normal().string());
		m_paletteFocusFrames = focus_frames;
	};

	void SpriteEditor::Relocate(const std::string& from, const std::string& to)
	{
		for (const std::unique_ptr<Document>& document : m_documents)
		{
			const std::string image = ProjectAssets::Relocate(document->links.image, from, to);
			const std::string path = ProjectAssets::Relocate(document->path, from, to);

			std::error_code code;

			// One not written yet goes wherever its image goes.
			document->path = path == document->path && image != document->links.image && !std::filesystem::exists(path, code)
				? SpriteAsset::For(image)
				: path;

			const auto relocate = [&](SpriteAsset::Links& links)
			{
				links.image = ProjectAssets::Relocate(links.image, from, to);
				links.palette = ProjectAssets::Relocate(links.palette, from, to);
			};

			relocate(document->links);
			relocate(document->savedLinks);

			for (std::vector<Snapshot>* history : { &document->undo, &document->redo })
				for (Snapshot& snapshot : *history)
					relocate(snapshot.links);
		};

		m_loosePalette = ProjectAssets::Relocate(m_loosePalette, from, to);

		std::map<std::string, PaletteFile> palettes;

		for (auto& [path, file] : m_palettes)
			palettes[ProjectAssets::Relocate(path, from, to)] = std::move(file);

		m_palettes = std::move(palettes);
	};

	std::vector<std::string> SpriteEditor::TakeSaved()
	{
		return std::exchange(m_saved, { });
	};

	std::vector<std::pair<std::string, std::string>> SpriteEditor::TakeRenamed()
	{
		return std::exchange(m_renamed, { });
	};

	#pragma endregion

	#pragma region Windows

	void SpriteEditor::Draw(bool* open, bool* palette_open)
	{
		if (m_focusFrames > 0)
		{
			m_focusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_focused = false;

		// As the Palette panel was last drawn, which is after this one.
		const bool palette_shown = std::exchange(m_paletteShown, false);

		DockPalette();

		// The wheel zooms the canvas rather than scrolling the panel.
		if (ImGui::Begin(EditorPanels::Sprites, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (ImGui::IsWindowAppearing() && !*palette_open)
				m_askPalette = true;

			if (m_documents.empty())
				ImGui::TextDisabled("Click an image or a sprite in the 2D tab to edit it here.");
			else if (ImGui::BeginTabBar("##sprites", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
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
					const std::string label = std::string(EditorIcons::Sprite) + " " + FileName(document->path) + "###" + std::to_string((uintptr_t)document.get());
					bool keep = true;
					const int first_vertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
					const bool shown = ImGui::BeginTabItem(label.c_str(), &keep, flags);

					// Short of the close button, which sits a line wide at the right.
					if (document->preview)
						Italicise(first_vertex, ImGui::GetItemRectMax().x - ImGui::GetStyle().FramePadding.x - ImGui::GetFontSize());

					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						document->preview = false;

					ImGui::SetItemTooltip("%s\n%s", document->path.c_str(), document->links.image.c_str());

					if (shown)
					{
						m_active = document.get();
						DrawDocument(*document, palette_open, palette_shown);
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
			DrawPaletteQuestion(palette_open, ImGui::GetWindowDockID());
		};

		ImGui::End();
	};

	void SpriteEditor::DrawPaletteQuestion(bool* palette_open, ImGuiID dock)
	{
		constexpr const char* popup = "Open Palette?";

		if (m_askPalette && !ImGui::IsPopupOpen(popup))
			ImGui::OpenPopup(popup);

		if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (!m_askPalette || *palette_open)
		{
			m_askPalette = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		};

		ImGui::Text("%s Open the Palette panel under the sprites?", EditorIcons::Palette);

		const bool open = ImGui::Button("Open Palette");
		ImGui::SameLine();
		const bool skip = ImGui::Button("Not Now");

		if (open)
		{
			*palette_open = true;
			m_paletteFocusFrames = focus_frames;
			m_paletteDock = dock;
		};

		if (open || skip)
		{
			m_askPalette = false;
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndPopup();
	};

	void SpriteEditor::DockPalette()
	{
		const ImGuiID node = std::exchange(m_paletteDock, 0);

		if (node == 0 || ImGui::DockBuilderGetNode(node) == nullptr)
			return;

		ImGuiID above = 0;
		const ImGuiID below = ImGui::DockBuilderSplitNode(node, ImGuiDir_Down, palette_share, nullptr, &above);

		ImGui::DockBuilderDockWindow(EditorPanels::Palette, below);
		ImGui::DockBuilderFinish(node);
	};

	void SpriteEditor::DrawDocument(Document& document, bool* palette_open, bool palette_shown)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
			SaveDocument(document);

		// Asked every frame, mid-stroke too: ImGui only routes a shortcut to a
		// window that asked for it the frame before.
		const bool undo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z);
		const bool redo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);

		// Not while a layer is held either, which knows it by where it is.
		if (!document.stroking && document.grabbed < 0 && (undo || redo))
			Step(document, undo);

		ToolShortcuts();
		Play(document);

		const ImGuiStyle& style = ImGui::GetStyle();
		const float line = ImGui::GetFrameHeightWithSpacing();
		const float rows = (float)(std::min)((int)document.sprite.layers.size(), timeline_rows);

		const float timeline_height = line * (rows + timeline_head_rows) + style.ScrollbarSize;
		const float status_height = line;
		const ImVec2 available = ImGui::GetContentRegionAvail();

		DrawCanvas(document, ImVec2(available.x, available.y - timeline_height - status_height));
		DrawTimeline(document, timeline_height);
		DrawStatus(document, palette_open, palette_shown);
	};

	#pragma endregion

	#pragma region Canvas

	void SpriteEditor::DrawCanvas(Document& document, const ImVec2& size)
	{
		if (size.x < 1.0f || size.y < 1.0f)
			return;

		const ImVec2 corner = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

		const bool hovered = ImGui::IsItemHovered();
		const ImGuiIO& io = ImGui::GetIO();

		if (document.zoom == 0.0f)
			Fit(document, size);

		if (hovered && io.MouseWheel != 0.0f)
			Zoom(document, ImVec2(io.MousePos.x - corner.x, io.MousePos.y - corner.y), io.MouseWheel);

		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
		{
			document.offset.x += io.MouseDelta.x;
			document.offset.y += io.MouseDelta.y;
		};

		const float zoom = document.zoom;
		const ImVec2 origin(std::floor(corner.x + document.offset.x), std::floor(corner.y + document.offset.y));

		const int mouse_x = (int)std::floor((io.MousePos.x - origin.x) / zoom);
		const int mouse_y = (int)std::floor((io.MousePos.y - origin.y) / zoom);

		std::optional<ImGuiMouseButton> pressed;

		for (const ImGuiMouseButton button : paint_buttons)
			if (hovered && ImGui::IsMouseClicked(button))
				pressed = button;

		Paint(document, mouse_x, mouse_y, pressed);

		const Image& image = Shown(document);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 end(corner.x + size.x, corner.y + size.y);

		draw->PushClipRect(corner, end, true);

		const ImVec2 image_end(origin.x + image.width * zoom, origin.y + image.height * zoom);
		const ImVec2 shown_min((std::max)(origin.x, corner.x), (std::max)(origin.y, corner.y));
		const ImVec2 shown_max((std::min)(image_end.x, end.x), (std::min)(image_end.y, end.y));

		// Squares counted from the image's corner, so they move with it.
		for (float y = origin.y + std::floor((shown_min.y - origin.y) / checker_size) * checker_size; y < shown_max.y; y += checker_size)
			for (float x = origin.x + std::floor((shown_min.x - origin.x) / checker_size) * checker_size; x < shown_max.x; x += checker_size)
			{
				const bool light = (((int)std::lround((x - origin.x) / checker_size) + (int)std::lround((y - origin.y) / checker_size)) & 1) == 0;

				draw->AddRectFilled(
					ImVec2((std::max)(x, origin.x), (std::max)(y, origin.y)),
					ImVec2((std::min)(x + checker_size, image_end.x), (std::min)(y + checker_size, image_end.y)),
					light ? checker_light : checker_dark);
			};

		const int first_x = (std::max)(0, (int)std::floor((shown_min.x - origin.x) / zoom));
		const int first_y = (std::max)(0, (int)std::floor((shown_min.y - origin.y) / zoom));
		const int last_x = (std::min)(image.width, (int)std::ceil((shown_max.x - origin.x) / zoom));
		const int last_y = (std::min)(image.height, (int)std::ceil((shown_max.y - origin.y) / zoom));

		const int step = (std::max)(1, ((std::max)(last_x - first_x, last_y - first_y) + max_cells - 1) / max_cells);

		// A run of one colour along a row is one rectangle.
		for (int y = first_y; y < last_y; y += step)
		{
			const float top = origin.y + y * zoom;
			const float bottom = origin.y + (std::min)(y + step, image.height) * zoom;

			for (int x = first_x; x < last_x;)
			{
				const Colour colour = image.Get(x, y);

				int run = x + step;

				while (run < last_x && image.Get(run, y) == colour)
					run += step;

				if (colour[alpha] != 0)
					draw->AddRectFilled(
						ImVec2(origin.x + x * zoom, top),
						ImVec2(origin.x + (std::min)(run, image.width) * zoom, bottom),
						ToImGui(colour));

				x = run;
			};
		};

		if (m_grid && zoom >= grid_zoom)
		{
			for (int x = first_x; x <= last_x; x++)
				draw->AddLine(ImVec2(origin.x + x * zoom, shown_min.y), ImVec2(origin.x + x * zoom, shown_max.y), grid_colour);

			for (int y = first_y; y <= last_y; y++)
				draw->AddLine(ImVec2(shown_min.x, origin.y + y * zoom), ImVec2(shown_max.x, origin.y + y * zoom), grid_colour);
		};

		// Where an outlined layer's pixel meets one it does not cover, or the
		// image's edge. Not while a cell is a block of pixels.
		if (step == 1)
			for (const SpriteAsset::Layer& layer : document.sprite.layers)
			{
				if (!layer.outlined)
					continue;

				const Image& cel = layer.cels[document.frame];
				const auto covers = [&cel](int x, int y) { return cel.Contains(x, y) && cel.Get(x, y)[alpha] != 0; };

				for (int y = first_y; y < last_y; y++)
					for (int x = first_x; x < last_x; x++)
					{
						if (!covers(x, y))
							continue;

						const ImVec2 min(origin.x + x * zoom, origin.y + y * zoom);
						const ImVec2 max(min.x + zoom, min.y + zoom);

						if (!covers(x - 1, y))
							draw->AddLine(min, ImVec2(min.x, max.y), outline_colour, outline_thickness);

						if (!covers(x + 1, y))
							draw->AddLine(ImVec2(max.x, min.y), max, outline_colour, outline_thickness);

						if (!covers(x, y - 1))
							draw->AddLine(min, ImVec2(max.x, min.y), outline_colour, outline_thickness);

						if (!covers(x, y + 1))
							draw->AddLine(ImVec2(min.x, max.y), max, outline_colour, outline_thickness);
					};
			};

		draw->AddRect(origin, image_end, border_colour);

		document.hovering = hovered && image.Contains(mouse_x, mouse_y);
		document.hoverX = mouse_x;
		document.hoverY = mouse_y;

		if (document.hovering)
			draw->AddRect(
				ImVec2(origin.x + mouse_x * zoom, origin.y + mouse_y * zoom),
				ImVec2(origin.x + (mouse_x + 1) * zoom, origin.y + (mouse_y + 1) * zoom),
				hover_colour);

		draw->PopClipRect();
	};

	#pragma endregion

	#pragma region Timeline

	void SpriteEditor::DrawTimeline(Document& document, float height)
	{
		SpriteAsset& sprite = document.sprite;
		const int layers = (int)sprite.layers.size();
		const int frames = sprite.Frames();

		if (ImGui::Button("+ Layer"))
		{
			Remember(document);
			sprite.AddLayer(document.layer + 1, "Layer " + std::to_string(layers + 1));
			document.layer++;
			Edited(document);
		};

		ImGui::SetItemTooltip("A clear layer above the one painted");
		ImGui::SameLine();
		ImGui::BeginDisabled(layers == 1);

		if (ImGui::Button("- Layer"))
		{
			Remember(document);
			sprite.layers.erase(sprite.layers.begin() + document.layer);
			document.layer = (std::min)(document.layer, layers - 2);
			Edited(document);
		};

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::BeginDisabled(frames >= SpriteAsset::maxFrames);

		if (ImGui::Button("+ Frame"))
		{
			Remember(document);
			sprite.AddFrame(document.frame + 1, false);
			document.frame++;
			Edited(document);
		};

		ImGui::SetItemTooltip("A clear frame after this one");
		ImGui::SameLine();

		if (ImGui::Button("Copy Frame"))
		{
			Remember(document);
			sprite.AddFrame(document.frame + 1, true);
			document.frame++;
			Edited(document);
		};

		ImGui::SetItemTooltip("This frame again, after it");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(frames == 1);

		if (ImGui::Button("- Frame"))
		{
			Remember(document);
			sprite.RemoveFrame(document.frame);
			document.frame = (std::min)(document.frame, frames - 2);
			Edited(document);
		};

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::BeginDisabled(frames == 1 && !document.playing);

		if (ImGui::Button(document.playing ? "Stop" : "Play"))
		{
			document.playing = !document.playing;
			document.played = 0.0f;
		};

		ImGui::EndDisabled();
		ImGui::SameLine();

		// One step to undo for each drag or typed value, as for a stroke.
		int duration = sprite.durations[document.frame];
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * name_lines);
		const bool timed = ImGui::DragInt("##duration", &duration, 1.0f, SpriteAsset::minDuration, SpriteAsset::maxDuration, "%d ms", ImGuiSliderFlags_AlwaysClamp);

		if (ImGui::IsItemActivated())
			Remember(document);

		if (timed)
			sprite.durations[document.frame] = duration;

		if (ImGui::IsItemDeactivated())
			Settle(document);

		ImGui::SetItemTooltip("How long this frame shows");

		// Counted again, since the buttons may have just changed them.
		const int shown_layers = (int)sprite.layers.size();
		const int shown_frames = sprite.Frames();

		const ImGuiTableFlags flags =
			ImGuiTableFlags_ScrollX |
			ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingFixedFit;

		const float font = ImGui::GetFontSize();

		if (!ImGui::BeginTable("##timeline", shown_frames + 1, flags, ImVec2(0.0f, height - ImGui::GetFrameHeightWithSpacing())))
			return;

		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Layers", ImGuiTableColumnFlags_WidthFixed, font * layers_lines);

		for (int frame = 0; frame < shown_frames; frame++)
			ImGui::TableSetupColumn(std::to_string(frame + 1).c_str(), ImGuiTableColumnFlags_WidthFixed, font * frame_lines);

		// The frames' numbers pick the frame.
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("Layers");

		for (int frame = 0; frame < shown_frames; frame++)
		{
			ImGui::TableSetColumnIndex(frame + 1);
			ImGui::PushID(frame);

			if (ImGui::Selectable(std::to_string(frame + 1).c_str(), frame == document.frame))
			{
				document.frame = frame;
				document.stale = true;
			};

			ImGui::PopID();
		};

		// The row the mouse is over, for a held layer to move to once the rows
		// are drawn, since they hold the layers. Only rows in sight count, the
		// top and bottom of which are kept for a layer held past them.
		struct ShownRow
		{
			int layer;
			float edge;
		};

		std::optional<int> under;
		std::optional<ShownRow> top_shown;
		std::optional<ShownRow> bottom_shown;

		// The top layer first, as they stack.
		for (int layer = shown_layers - 1; layer >= 0; layer--)
		{
			SpriteAsset::Layer& row = sprite.layers[layer];

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(layer);

			// The grip, for dragging the layer up or down past the others,
			// labelled by the layer for a UiScript to aim at.
			const float box = ImGui::GetFrameHeight();

			ImGui::InvisibleButton(("##grip " + row.name).c_str(), ImVec2(box, box));

			if (ImGui::IsItemActivated())
			{
				document.layer = layer;
				document.grabbed = layer;
				document.grabMoved = false;
			};

			{
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				const ImVec2 centre((min.x + max.x) * half, (min.y + max.y) * half);
				const float spacing = box / (grip_rows + 1);
				const bool lit = document.grabbed == layer || ImGui::IsItemHovered();
				const ImU32 colour = ImGui::GetColorU32(lit ? ImGuiCol_Text : ImGuiCol_TextDisabled);
				ImDrawList* draw = ImGui::GetWindowDrawList();

				draw->AddRectFilled(min, max, ImGui::GetColorU32(lit ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);

				for (int column = 0; column < grip_columns; column++)
					for (int line = 0; line < grip_rows; line++)
						draw->AddCircleFilled(
							ImVec2(centre.x + (column - (grip_columns - 1) * half) * spacing, centre.y + (line - (grip_rows - 1) * half) * spacing),
							font * grip_dot,
							colour);
			};

			if (ImGui::IsItemHovered() && document.grabbed < 0)
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

			ImGui::SetItemTooltip("Drag up or down to move the layer");

			if (ImGui::IsItemVisible())
			{
				const float top = ImGui::GetItemRectMin().y;
				const float bottom = ImGui::GetItemRectMax().y;

				if (const float mouse_y = ImGui::GetIO().MousePos.y; mouse_y >= top && mouse_y < bottom)
					under = layer;

				if (!top_shown)
					top_shown = ShownRow{ layer, top };

				bottom_shown = ShownRow{ layer, bottom };
			};

			ImGui::SameLine();
			bool visible = row.visible;

			if (ImGui::Checkbox("##visible", &visible))
			{
				Remember(document);
				row.visible = visible;
				Edited(document);
			};

			ImGui::SetItemTooltip(visible ? "Shown, and saved into the image" : "Hidden, and left out of the image");
			ImGui::SameLine();

			// A square drawn as the outline is, bright while it is on.
			if (ImGui::InvisibleButton("##outlined", ImVec2(box, box)))
			{
				Remember(document);
				row.outlined = !row.outlined;
				Edited(document);
			};

			{
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				const float inset = ImGui::GetStyle().FramePadding.y;
				ImDrawList* draw = ImGui::GetWindowDrawList();

				draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
				draw->AddRect(
					ImVec2(min.x + inset, min.y + inset),
					ImVec2(max.x - inset, max.y - inset),
					row.outlined ? outline_colour : ImGui::GetColorU32(ImGuiCol_TextDisabled),
					0.0f,
					0,
					outline_thickness);
			};

			ImGui::SetItemTooltip(row.outlined ? "Outlined on the canvas" : "Outline its pixels on the canvas");
			ImGui::SameLine();

			if (ImGui::Selectable(row.name.c_str(), layer == document.layer))
				document.layer = layer;

			ImGui::SetItemTooltip("Right-click to rename");

			if (ImGui::BeginPopupContextItem("##rename"))
			{
				if (ImGui::IsWindowAppearing())
				{
					snprintf(m_layerName, sizeof(m_layerName), "%s", row.name.c_str());
					ImGui::SetKeyboardFocusHere();
				};

				if (ImGui::InputText("##name", m_layerName, sizeof(m_layerName), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (row.name != m_layerName)
					{
						Remember(document);
						row.name = m_layerName;
						Edited(document);
					};

					ImGui::CloseCurrentPopup();
				};

				ImGui::EndPopup();
			};

			for (int frame = 0; frame < shown_frames; frame++)
			{
				ImGui::TableSetColumnIndex(frame + 1);
				ImGui::PushID(frame);

				if (ImGui::Selectable("##cel", layer == document.layer && frame == document.frame))
				{
					document.layer = layer;
					document.frame = frame;
					document.stale = true;
				};

				ImGui::PopID();
			};

			ImGui::PopID();
		};

		// Held past the rows in sight, the layer goes to the one at that edge
		// and the rows scroll on to bring more into sight.
		if (document.grabbed >= 0 && !under && top_shown)
		{
			const float mouse_y = ImGui::GetIO().MousePos.y;
			const float step = ImGui::GetIO().DeltaTime * held_scroll_rows * ImGui::GetFrameHeightWithSpacing();

			if (mouse_y < top_shown->edge)
			{
				ImGui::SetScrollY((std::max)(0.0f, ImGui::GetScrollY() - step));
				under = top_shown->layer;
			}
			else if (mouse_y >= bottom_shown->edge)
			{
				ImGui::SetScrollY((std::min)(ImGui::GetScrollMaxY(), ImGui::GetScrollY() + step));
				under = bottom_shown->layer;
			};
		};

		ImGui::EndTable();

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			document.grabbed = -1;

		if (document.grabbed < 0)
			return;

		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

		if (under && *under != document.grabbed)
		{
			if (!std::exchange(document.grabMoved, true))
				Remember(document);

			sprite.MoveLayer(document.grabbed, *under);
			document.grabbed = document.layer = *under;
			Edited(document);
		};
	};

	#pragma endregion

	#pragma region Status

	void SpriteEditor::DrawStatus(Document& document, bool* palette_open, bool palette_shown)
	{
		if (document.outside)
		{
			ImGui::Text("%s changed outside the sprite", FileName(document.links.image).c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Take It In"))
			{
				Remember(document);
				document.sprite.TakeIn(std::move(*document.outside), FileName(document.links.image));
				document.layer = (int)document.sprite.layers.size() - 1;
				document.frame = (std::min)(document.frame, document.sprite.Frames() - 1);
				document.outside.reset();
				Edited(document);
			};

			ImGui::SetItemTooltip("As a new layer, cut into the frames when it is the size of their sheet");
			ImGui::SameLine();

			if (ImGui::SmallButton("Keep Sprite"))
				document.outside.reset();

			ImGui::SetItemTooltip("Saving writes the sprite over the image");
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
		};

		// Asked of the panel only while the palette is out of sight, since
		// painting needs what it holds.
		if (!palette_shown)
		{
			ImGui::TextDisabled("%s No palette open", EditorIcons::Palette);
			ImGui::SameLine();

			if (ImGui::SmallButton("Open Palette"))
			{
				*palette_open = true;
				m_paletteFocusFrames = focus_frames;
			};

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
		};

		ImGui::Checkbox("Grid", &m_grid);
		ImGui::SameLine();

		// Fitted again once the canvas is next drawn, at its size then.
		if (ImGui::SmallButton("Fit"))
			document.zoom = 0.0f;

		ImGui::SameLine();

		if (ImGui::SmallButton("Resize"))
		{
			m_resizeWidth = document.sprite.width;
			m_resizeHeight = document.sprite.height;
			m_resizeAnchor = middle_anchor;
			ImGui::OpenPopup(resize_popup);
		};

		DrawResize(document);
		ImGui::SameLine();
		ImGui::TextDisabled(
			"%d x %d, %g%%, frame %d of %d",
			document.sprite.width,
			document.sprite.height,
			document.zoom * 100.0f,
			document.frame + 1,
			document.sprite.Frames());

		if (document.hovering)
		{
			const Colour colour = Shown(document).Get(document.hoverX, document.hoverY);

			ImGui::SameLine();
			ImGui::TextDisabled("  %d, %d  #%02X%02X%02X%02X", document.hoverX, document.hoverY, colour[0], colour[1], colour[2], colour[3]);
		};
	};

	void SpriteEditor::DrawResize(Document& document)
	{
		if (!ImGui::BeginPopup(resize_popup))
			return;

		const float width = ImGui::GetFontSize() * resize_field_lines;

		ImGui::BeginGroup();
		ImGui::SetNextItemWidth(width);
		ImGui::InputInt("##Width", &m_resizeWidth, 0);
		ImGui::SetItemTooltip("Width");
		ImGui::SameLine();
		ImGui::TextUnformatted("x");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(width);
		ImGui::InputInt("##Height", &m_resizeHeight, 0);
		ImGui::SetItemTooltip("Height");

		m_resizeWidth = std::clamp(m_resizeWidth, 1, SpriteAsset::maxSide);
		m_resizeHeight = std::clamp(m_resizeHeight, 1, SpriteAsset::maxSide);

		ImGui::Checkbox("Scale", &m_resizeScale);
		ImGui::SetItemTooltip(m_resizeScale ? "The pixels stretch or shrink to the size" : "The pixels stay as they are, cut or grown round the anchor");
		ImGui::EndGroup();

		ImGui::SameLine();
		ImGui::BeginDisabled(m_resizeScale);
		ImGui::BeginGroup();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

		for (int anchor = 0; anchor < anchor_columns * anchor_columns; anchor++)
		{
			ImGui::PushID(anchor);

			if (ImGui::RadioButton("##anchor", m_resizeAnchor == anchor))
				m_resizeAnchor = anchor;

			ImGui::SetItemTooltip("Anchor");
			ImGui::PopID();

			if (anchor % anchor_columns != anchor_columns - 1)
				ImGui::SameLine();
		};

		ImGui::PopStyleVar(2);
		ImGui::EndGroup();
		ImGui::EndDisabled();
		ImGui::BeginDisabled(m_resizeWidth == document.sprite.width && m_resizeHeight == document.sprite.height);

		if (ImGui::SmallButton("OK"))
		{
			constexpr float last_column = anchor_columns - 1;

			Remember(document);
			document.sprite.Resize(
				m_resizeWidth,
				m_resizeHeight,
				m_resizeScale,
				(float)(m_resizeAnchor % anchor_columns) / last_column,
				(float)(m_resizeAnchor / anchor_columns) / last_column);
			Edited(document);

			// Fitted again at the new size.
			document.zoom = 0.0f;
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndDisabled();
		ImGui::SameLine();

		if (ImGui::SmallButton("Cancel"))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	};

	#pragma endregion

	#pragma region Palette Window

	void SpriteEditor::DrawPalette(bool* open, const std::string& project_folder)
	{
		if (m_paletteFocusFrames > 0)
		{
			m_paletteFocusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_paletteFocused = false;

		if (ImGui::Begin(EditorPanels::Palette, open))
		{
			m_paletteShown = true;
			m_paletteFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			ToolShortcuts();

			// Asked every frame, as the Sprites panel's are.
			const bool save = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S);
			const bool undo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z);
			const bool redo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
			const std::string path = PalettePath();

			if (path.empty() && m_active != nullptr && (undo || redo))
				Step(*m_active, undo);
			else if (!path.empty())
			{
				if ((undo || redo) && !StepPalette(PaletteAt(path), undo) && m_active != nullptr)
					Step(*m_active, undo);

				if (save)
					SavePalette(path);
			};

			DrawTools();
			ImGui::Separator();
			DrawPaletteChoice(project_folder);

			if (!PalettePath().empty())
				DrawSwatches(PalettePath());
			else if (m_active != nullptr)
				ImGui::TextDisabled("No palette yet. New makes one of the sprite's colours.");
			else
				ImGui::TextDisabled("Open a sprite to paint it.");

			DrawPaletteMenu(project_folder);
		};

		ImGui::End();
	};

	void SpriteEditor::DrawTools()
	{
		const float side = ImGui::GetFrameHeight();

		for (const ToolInfo& info : tools)
		{
			const bool chosen = m_tool == info.tool;

			if (chosen)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));

			if (IconButton(info.icon, info.label, side))
				m_tool = info.tool;

			if (chosen)
				ImGui::PopStyleColor();

			ImGui::SetItemTooltip("%s (%s)", info.label, ImGui::GetKeyName(info.key));
			ImGui::SameLine();
		};

		for (const ImGuiMouseButton button : paint_buttons)
		{
			Colour& chosen = m_colours[button];
			float colour[channels];

			for (int i = 0; i < channels; i++)
				colour[i] = chosen[i] / (float)UINT8_MAX;

			if (ImGui::ColorEdit4((std::string("##") + button_names[button]).c_str(), colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
				for (int i = 0; i < channels; i++)
					chosen[i] = (uint8_t)std::lround(colour[i] * UINT8_MAX);

			ImGui::SetItemTooltip("What the %s mouse button paints with (%s swaps them)", button_names[button], ImGui::GetKeyName(swap_key));
			ImGui::SameLine();
		};

		const std::string& path = PalettePath();

		ImGui::BeginDisabled(m_active == nullptr || path.empty());

		if (IconButton(import_icon, "Import", side))
		{
			PaletteFile& file = PaletteAt(path);
			const std::string name = std::filesystem::path(m_active->path).stem().string();

			RememberPalette(file);
			file.palette.categories.push_back({ name, m_active->sprite.Colours(std::numeric_limits<size_t>::max()) });
		};

		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Import the sprite's colours into a category of the palette");
	};

	void SpriteEditor::DrawPaletteChoice(const std::string& project_folder)
	{
		const std::string& current = PalettePath();
		const std::string shown = current.empty() ? "None" : FileName(current) + (PaletteAt(current).Unsaved() ? " *" : "");

		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttons_width = ImGui::CalcTextSize("New").x + ImGui::CalcTextSize("Clone").x + (style.FramePadding.x * 2.0f + style.ItemSpacing.x) * 2.0f;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons_width);

		if (ImGui::BeginCombo("##palette", (std::string(EditorIcons::Palette) + " " + shown).c_str()))
		{
			if (ImGui::IsWindowAppearing())
				FindPalettes(project_folder);

			if (ImGui::Selectable("None", current.empty()))
				SetPalette("");

			for (const std::string& file : m_paletteFiles)
			{
				if (ImGui::Selectable(FileName(file).c_str(), file == current))
					SetPalette(file);

				ImGui::SetItemTooltip("%s", file.c_str());
			};

			ImGui::EndCombo();
		};

		if (!current.empty())
			ImGui::SetItemTooltip("Right-click to rename");

		DrawPaletteRename(project_folder);

		ImGui::SameLine();
		ImGui::BeginDisabled(m_active == nullptr);

		if (ImGui::Button("New"))
		{
			const std::string path = NewPalettePath(m_active->path, [this](const std::string& taken) { return m_palettes.contains(taken); });

			m_palettes[path] = PaletteFile{ Palette{ m_active->sprite.Colours(seeded_colours) } };
			SetPalette(path);
		};

		ImGui::EndDisabled();
		ImGui::SetItemTooltip("A palette beside the sprite, of the colours it already has");

		ImGui::SameLine();
		ImGui::BeginDisabled(current.empty());

		if (ImGui::Button("Clone"))
		{
			const std::string path = NewPalettePath(current, [this](const std::string& taken) { return m_palettes.contains(taken); });

			m_palettes[path] = PaletteFile{ PaletteAt(current).palette };
			SetPalette(path);
		};

		ImGui::EndDisabled();
		ImGui::SetItemTooltip("A copy of this palette beside it, painted from in its place");
	};

	void SpriteEditor::DrawPaletteRename(const std::string& project_folder)
	{
		const std::string current = PalettePath();

		if (current.empty() || !ImGui::BeginPopupContextItem("##rename_palette"))
			return;

		if (ImGui::IsWindowAppearing())
		{
			snprintf(m_paletteName, sizeof(m_paletteName), "%s", std::filesystem::path(current).stem().string().c_str());
			ImGui::SetKeyboardFocusHere();
		};

		const bool entered = ImGui::InputText("##name", m_paletteName, sizeof(m_paletteName), ImGuiInputTextFlags_EnterReturnsTrue);
		const std::string name = std::string(m_paletteName) + Palette::extension;
		const std::filesystem::path target = std::filesystem::path(current).parent_path() / name;

		std::error_code code;
		const bool same = target == std::filesystem::path(current);
		const bool taken = !same && (m_palettes.contains(target.string()) || (std::filesystem::exists(target, code) && !std::filesystem::equivalent(current, target, code)));
		const bool usable = ProjectAssets::IsValidName(m_paletteName) && !taken;

		if (!usable)
			ImGui::TextDisabled(taken ? "%s is already there" : "Not a usable name", name.c_str());

		if (entered && (same || !usable))
		{
			if (same)
				ImGui::CloseCurrentPopup();

			ImGui::EndPopup();
			return;
		};

		if (entered && !std::filesystem::exists(current, code))
			Relocate(current, target.string());
		else if (entered)
		{
			std::string error;

			if (const std::string renamed = ProjectAssets::Rename(current, name, &error); renamed.empty())
				std::cerr << "Palette: " << error << std::endl;
			else
			{
				const std::string root = !project_folder.empty()
					? project_folder
					: std::filesystem::path(current).parent_path().string();

				SpriteAsset::RelinkPalettes(root, current, renamed);
				Relocate(current, renamed);
				m_renamed.emplace_back(current, renamed);
			};
		};

		if (entered)
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	};

	void SpriteEditor::DrawSwatches(const std::string& path)
	{
		DrawSection(path, PaletteAt(path).palette.colours, true, "+");
		DrawCategories(path);
	};

	void SpriteEditor::DrawSection(const std::string& path, std::vector<Colour>& colours, bool with_clear, const std::string& add_label)
	{
		PaletteFile& file = PaletteAt(path);

		const ImGuiStyle& style = ImGui::GetStyle();
		const float side = ImGui::GetFontSize() * swatch_lines;
		const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 marker = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32 marker_edge = ImGui::GetColorU32(ImGuiCol_WindowBg);
		const float corner = side * marker_share;

		std::vector<Colour> shown;

		if (with_clear)
			shown.push_back(clear);

		std::copy_if(colours.begin(), colours.end(), std::back_inserter(shown), [](const Colour& colour) { return !SeeThrough(colour); });

		for (size_t i = 0; i < shown.size(); i++)
		{
			const Colour& colour = shown[i];

			ImGui::PushID((int)i);

			if (colour == clear)
			{
				ImGui::ColorButton("##swatch", ToFloats(colour), ImGuiColorEditFlags_NoTooltip, ImVec2(side, side));
				ImGui::SetItemTooltip("Clear, which paints as the eraser does");
			}
			else
				ImGui::ColorButton("##swatch", ToFloats(colour), ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(side, side));

			ImGui::PopID();

			for (const ImGuiMouseButton button : paint_buttons)
				if (ImGui::IsItemClicked(button))
					m_colours[button] = colour;

			// The left colour's in the top left corner, the right's in the
			// bottom right.
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();

			if (Matches(colour, m_colours[ImGuiMouseButton_Left]))
			{
				draw->AddTriangleFilled(min, ImVec2(min.x + corner, min.y), ImVec2(min.x, min.y + corner), marker);
				draw->AddTriangle(min, ImVec2(min.x + corner, min.y), ImVec2(min.x, min.y + corner), marker_edge);
			};

			if (Matches(colour, m_colours[ImGuiMouseButton_Right]))
			{
				draw->AddTriangleFilled(max, ImVec2(max.x - corner, max.y), ImVec2(max.x, max.y - corner), marker);
				draw->AddTriangle(max, ImVec2(max.x - corner, max.y), ImVec2(max.x, max.y - corner), marker_edge);
			};

			if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + side <= right)
				ImGui::SameLine();
		};

		const Colour& left = m_colours[ImGuiMouseButton_Left];
		const auto found = std::find(colours.begin(), colours.end(), left);
		const bool listed = found != colours.end() && !SeeThrough(left);

		ImGui::BeginDisabled(SeeThrough(left));

		if (ImGui::Button(add_label.c_str(), ImVec2(side, side)))
		{
			RememberPalette(file);
			colours.push_back(left);
		};

		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Add the left colour here");
		ImGui::SameLine();
		ImGui::BeginDisabled(!listed);

		if (ImGui::Button("-", ImVec2(side, side)))
		{
			RememberPalette(file);
			colours.erase(found);
		};

		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Take the left colour out of here");
	};

	void SpriteEditor::DrawCategories(const std::string& path)
	{
		PaletteFile& file = PaletteAt(path);
		Palette& palette = file.palette;
		std::optional<size_t> deleted;

		for (size_t i = 0; i < palette.categories.size(); i++)
		{
			Palette::Category& category = palette.categories[i];

			ImGui::PushID((int)i);

			const bool open = ImGui::CollapsingHeader((category.name + "###category").c_str(), ImGuiTreeNodeFlags_DefaultOpen);

			if (m_renameCategory == (int)i)
			{
				ImGui::OpenPopup("##category");
				m_renameCategory = -1;
			};

			if (ImGui::BeginPopupContextItem("##category"))
			{
				if (ImGui::IsWindowAppearing())
				{
					snprintf(m_categoryName, sizeof(m_categoryName), "%s", category.name.c_str());
					ImGui::SetKeyboardFocusHere();
				};

				if (ImGui::InputText("##name", m_categoryName, sizeof(m_categoryName), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (m_categoryName[0] != '\0' && category.name != m_categoryName)
					{
						RememberPalette(file);
						category.name = m_categoryName;
					};

					ImGui::CloseCurrentPopup();
				};

				if (ImGui::MenuItem("Delete Category"))
					deleted = i;

				ImGui::EndPopup();
			};

			if (open)
				DrawSection(path, category.colours, false, "+##" + category.name);

			ImGui::PopID();
		};

		if (deleted)
		{
			RememberPalette(file);
			palette.categories.erase(palette.categories.begin() + *deleted);
		};
	};

	void SpriteEditor::DrawPaletteMenu(const std::string& project_folder)
	{
		if (!ImGui::BeginPopupContextWindow("##palette menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			return;

		const std::string& path = PalettePath();

		if (ImGui::MenuItem("New Category", nullptr, false, !path.empty()))
		{
			PaletteFile& file = PaletteAt(path);
			Palette& palette = file.palette;
			std::string name = new_category;

			for (int suffix = first_numbered; std::any_of(palette.categories.begin(), palette.categories.end(), [&name](const Palette::Category& category) { return category.name == name; }); suffix++)
				name = std::string(new_category) + " " + std::to_string(suffix);

			RememberPalette(file);
			palette.categories.push_back({ name, { } });
			m_renameCategory = (int)palette.categories.size() - 1;
		};

		if (ImGui::BeginMenu("Open Palette"))
		{
			if (ImGui::IsWindowAppearing())
				FindPalettes(project_folder);

			for (const std::string& file : m_paletteFiles)
			{
				if (ImGui::MenuItem(FileName(file).c_str(), nullptr, file == path))
					SetPalette(file);

				ImGui::SetItemTooltip("%s", file.c_str());
			};

			if (m_paletteFiles.empty())
				ImGui::MenuItem("None", nullptr, false, false);

			ImGui::EndMenu();
		};

		ImGui::EndPopup();
	};

	void SpriteEditor::FindPalettes(const std::string& project_folder)
	{
		m_paletteFiles.clear();

		const std::string root = !project_folder.empty() || m_active == nullptr
			? project_folder
			: std::filesystem::path(m_active->path).parent_path().string();

		std::error_code code;

		for (auto entry = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
			if (entry->path().extension() == Palette::extension)
				m_paletteFiles.push_back(entry->path().lexically_normal().string());

		for (const auto& [path, file] : m_palettes)
			if (!file.saved)
				m_paletteFiles.push_back(path);
	};

	#pragma endregion

	#pragma region Tool Shortcuts

	void SpriteEditor::ToolShortcuts()
	{
		for (const ToolInfo& info : tools)
			if (ImGui::Shortcut(info.key))
				m_tool = info.tool;

		if (ImGui::Shortcut(swap_key))
			std::swap(m_colours[ImGuiMouseButton_Left], m_colours[ImGuiMouseButton_Right]);
	};

	#pragma endregion

	#pragma region Palettes

	const std::string& SpriteEditor::PalettePath() const
	{
		return m_active != nullptr ? m_active->links.palette : m_loosePalette;
	};

	void SpriteEditor::SetPalette(const std::string& path)
	{
		if (m_active == nullptr)
		{
			m_loosePalette = path;
			return;
		};

		if (m_active->links.palette == path)
			return;

		Remember(*m_active);
		m_active->links.palette = path;
		Edited(*m_active);
	};

	SpriteEditor::PaletteFile& SpriteEditor::PaletteAt(const std::string& path)
	{
		const auto found = m_palettes.find(path);

		if (found != m_palettes.end())
			return found->second;

		// One that will not load shows empty, and is said once.
		PaletteFile& file = m_palettes[path];
		Palette::Load(path, file.palette);
		file.saved = file.palette;

		return file;
	};

	void SpriteEditor::RememberPalette(PaletteFile& file)
	{
		file.undo.push_back(file.palette);
		file.redo.clear();
	};

	bool SpriteEditor::StepPalette(PaletteFile& file, bool undo)
	{
		std::vector<Palette>& from = undo ? file.undo : file.redo;
		std::vector<Palette>& to = undo ? file.redo : file.undo;

		if (from.empty())
			return false;

		to.push_back(std::move(file.palette));
		file.palette = std::move(from.back());
		from.pop_back();
		return true;
	};

	bool SpriteEditor::SavePalette(const std::string& path)
	{
		PaletteFile& file = PaletteAt(path);

		if (!file.Unsaved())
			return true;

		if (!Palette::Save(path, file.palette))
			return false;

		file.saved = file.palette;

		return true;
	};

	#pragma endregion

	#pragma region Painting

	const SpriteAsset::Image& SpriteEditor::Shown(Document& document)
	{
		// A layer on its own is drawn as it is, with nothing to flatten while
		// it is painted.
		const Image* only = nullptr;
		int visible = 0;

		for (const SpriteAsset::Layer& layer : document.sprite.layers)
			if (layer.visible)
			{
				only = &layer.cels[document.frame];
				visible++;
			};

		if (visible == 1)
			return *only;

		if (std::exchange(document.stale, false))
			document.flattened = document.sprite.Flatten(document.frame);

		return document.flattened;
	};

	void SpriteEditor::Paint(Document& document, int x, int y, std::optional<ImGuiMouseButton> pressed)
	{
		Image& cel = document.sprite.layers[document.layer].cels[document.frame];

		// Before the tool is looked at, since it may have changed mid-stroke.
		// A stroke that changed nothing, such as one wholly off the image,
		// leaves nothing to undo.
		if (document.stroking && !ImGui::IsMouseDown(document.strokeButton))
		{
			document.stroking = false;
			Settle(document);
			return;
		};

		// The other button, mid-stroke, starts nothing.
		if (document.stroking)
			pressed.reset();

		switch (m_tool)
		{
		case Tool::Picker:
			if (pressed && Shown(document).Contains(x, y))
			{
				m_colours[*pressed] = Shown(document).Get(x, y);
				m_tool = Tool::Pencil;
			};
			return;

		case Tool::Fill:
			if (pressed && cel.Contains(x, y) && cel.Get(x, y) != Painted(m_colours[*pressed]))
			{
				Remember(document);
				cel.Fill(x, y, Painted(m_colours[*pressed]));
				Edited(document);
			};
			return;

		case Tool::Pencil:
		case Tool::Eraser:
			break;
		};

		if (pressed)
		{
			Remember(document);
			document.stroking = true;
			document.strokeButton = *pressed;
			document.lastX = x;
			document.lastY = y;
		};

		if (!document.stroking)
			return;

		const Colour colour = m_tool == Tool::Eraser ? clear : Painted(m_colours[document.strokeButton]);

		cel.Line(document.lastX, document.lastY, x, y, colour);
		document.lastX = x;
		document.lastY = y;
		document.unsaved = true;
		document.stale = true;
	};

	#pragma endregion

	#pragma region Playback

	void SpriteEditor::Play(Document& document)
	{
		if (!document.playing || document.sprite.Frames() == 1)
		{
			document.playing = false;
			return;
		};

		document.played += ImGui::GetIO().DeltaTime * milliseconds;

		while (document.played >= (float)document.sprite.durations[document.frame])
		{
			document.played -= (float)document.sprite.durations[document.frame];
			document.frame = (document.frame + 1) % document.sprite.Frames();
			document.stale = true;
		};
	};

	#pragma endregion

	#pragma region History

	void SpriteEditor::Remember(Document& document)
	{
		const size_t bytes = (std::max)(document.sprite.Bytes(), (size_t)1);
		const size_t kept = (std::max)(max_undo_bytes / bytes, (size_t)1);

		document.undo.push_back({ document.sprite, document.links });
		document.redo.clear();

		if (document.undo.size() > kept)
			document.undo.erase(document.undo.begin(), document.undo.end() - (ptrdiff_t)kept);
	};

	void SpriteEditor::Settle(Document& document)
	{
		if (!document.undo.empty() && document.undo.back().sprite == document.sprite && document.undo.back().links == document.links)
			document.undo.pop_back();

		Edited(document);
	};

	void SpriteEditor::Edited(Document& document)
	{
		document.unsaved = document.sprite != document.saved || document.links != document.savedLinks;
		document.stale = true;
		document.preview = false;
	};

	void SpriteEditor::Step(Document& document, bool undo)
	{
		std::vector<Snapshot>& from = undo ? document.undo : document.redo;
		std::vector<Snapshot>& to = undo ? document.redo : document.undo;

		if (from.empty())
			return;

		to.push_back({ std::move(document.sprite), std::move(document.links) });
		document.sprite = std::move(from.back().sprite);
		document.links = std::move(from.back().links);
		from.pop_back();

		// The layer or frame picked may be gone.
		document.layer = (std::min)(document.layer, (int)document.sprite.layers.size() - 1);
		document.frame = (std::min)(document.frame, document.sprite.Frames() - 1);

		Edited(document);
	};

	#pragma endregion

	#pragma region View

	void SpriteEditor::Fit(Document& document, const ImVec2& size)
	{
		const SpriteAsset& sprite = document.sprite;

		float zoom = (std::min)(size.x / sprite.width, size.y / sprite.height) * fit_share;

		if (zoom >= 1.0f)
			zoom = std::floor(zoom);

		document.zoom = std::clamp(zoom, min_zoom, max_zoom);
		document.offset = ImVec2(
			std::floor((size.x - sprite.width * document.zoom) * half),
			std::floor((size.y - sprite.height * document.zoom) * half));
	};

	void SpriteEditor::Zoom(Document& document, const ImVec2& about, float wheel)
	{
		const float was = document.zoom;

		float zoom = was * std::pow(wheel_zoom, wheel);

		// At least one whole step, or a slow wheel would round back to where
		// it was.
		if (zoom >= 1.0f)
			zoom = wheel > 0.0f
				? (std::max)(std::round(zoom), std::floor(was) + 1.0f)
				: (std::min)(std::round(zoom), std::ceil(was) - 1.0f);

		zoom = std::clamp(zoom, min_zoom, max_zoom);

		// The pixel under the mouse stays under it.
		document.offset.x = about.x - (about.x - document.offset.x) * zoom / was;
		document.offset.y = about.y - (about.y - document.offset.y) * zoom / was;
		document.zoom = zoom;
	};

	#pragma endregion

	#pragma region Saving

	bool SpriteEditor::SaveAll()
	{
		bool saved = true;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->unsaved)
				saved = SaveDocument(*document) && saved;

		// One made by New and since let go of is not written.
		const auto linked = [this](const std::string& path)
		{
			return path == m_loosePalette || std::any_of(m_documents.begin(), m_documents.end(),
				[&path](const std::unique_ptr<Document>& document) { return document->links.palette == path; });
		};

		for (const auto& [path, file] : m_palettes)
			if (file.saved || linked(path))
				saved = SavePalette(path) && saved;

		return saved;
	};

	bool SpriteEditor::SaveDocument(Document& document)
	{
		if (document.outside)
		{
			std::cerr << "Sprites: " << document.links.image << " changed outside the sprite. Take it in, or keep the sprite, to save" << std::endl;
			return false;
		};

		// The palette before the sprite that names it, and the image before
		// the sprite, since it is what the game draws.
		if ((!document.links.palette.empty() && !SavePalette(document.links.palette)) ||
			!SpriteAsset::SaveImage(document.links.image, document.sprite.Sheet()) ||
			!SpriteAsset::Save(document.path, document.sprite, document.links))
			return false;

		document.saved = document.sprite;
		document.savedLinks = document.links;
		document.unsaved = false;
		m_saved.push_back(document.links.image);

		return true;
	};

	#pragma endregion

	#pragma region Closing

	void SpriteEditor::DrawCloseQuestion()
	{
		constexpr const char* popup = "Unsaved Sprite";

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

		ImGui::Text("Save the changes to %s?", FileName(m_closing->path).c_str());

		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		// A save that fails leaves the question up, with the edits still open.
		const bool saved = save && SaveDocument(*m_closing);

		if (saved || discard)
			Close(m_closing);

		if (saved || discard || cancel)
		{
			m_closing = nullptr;
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndPopup();
	};

	void SpriteEditor::Close(const Document* document)
	{
		if (m_active == document)
			m_active = nullptr;

		std::erase_if(m_documents, [document](const std::unique_ptr<Document>& open) { return open.get() == document; });
	};

	#pragma endregion
};

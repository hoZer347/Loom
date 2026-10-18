#include "SpriteEditor.h"

#include "EditorIcons.h"
#include "ProjectAssets.h"
#include "Textures.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr int channels = 4;
		constexpr int alpha = 3;

		// A window docked on the frame it first appears only takes its tab on
		// the one after.
		constexpr int focus_frames = 2;

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

		constexpr int jpeg_quality = 95;

		// Shared by a document's strokes, so a large image keeps fewer of them.
		constexpr size_t max_undo_bytes = 256 * 1024 * 1024;

		struct ToolInfo
		{
			SpriteEditor::Tool tool;
			const char* label;
			ImGuiKey key;
		};

		constexpr ToolInfo tools[] =
		{
			{ SpriteEditor::Tool::Pencil, "Pencil", ImGuiKey_B },
			{ SpriteEditor::Tool::Eraser, "Eraser", ImGuiKey_E },
			{ SpriteEditor::Tool::Fill, "Fill", ImGuiKey_G },
			{ SpriteEditor::Tool::Picker, "Picker", ImGuiKey_I },
		};

		std::string LowerExtension(const std::string& path)
		{
			std::string extension = std::filesystem::path(path).extension().string();

			std::transform(
				extension.begin(),
				extension.end(),
				extension.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });

			return extension;
		};

		void FlipRows(SpriteEditor::Image& image)
		{
			const size_t row = (size_t)image.width * channels;

			for (int y = 0; y < image.height / 2; y++)
				std::swap_ranges(
					image.pixels.begin() + (ptrdiff_t)(y * row),
					image.pixels.begin() + (ptrdiff_t)((y + 1) * row),
					image.pixels.begin() + (ptrdiff_t)((image.height - 1 - y) * row));
		};

		ImU32 ToImGui(const SpriteEditor::Colour& colour)
		{
			return IM_COL32(colour[0], colour[1], colour[2], colour[3]);
		};
	};

	bool SpriteEditor::Image::Contains(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < width && y < height;
	};

	SpriteEditor::Colour SpriteEditor::Image::Get(int x, int y) const
	{
		const uint8_t* pixel = &pixels[((size_t)y * width + x) * channels];

		return { pixel[0], pixel[1], pixel[2], pixel[3] };
	};

	void SpriteEditor::Image::Set(int x, int y, const Colour& colour)
	{
		if (Contains(x, y))
			std::copy(colour.begin(), colour.end(), &pixels[((size_t)y * width + x) * channels]);
	};

	void SpriteEditor::Image::Line(int x0, int y0, int x1, int y1, const Colour& colour)
	{
		// Bresenham's, stepping along whichever axis the line covers more of.
		const int dx = std::abs(x1 - x0);
		const int dy = -std::abs(y1 - y0);
		const int sx = x0 < x1 ? 1 : -1;
		const int sy = y0 < y1 ? 1 : -1;

		int error = dx + dy;

		while (true)
		{
			Set(x0, y0, colour);

			if (x0 == x1 && y0 == y1)
				return;

			const int doubled = error + error;

			if (doubled >= dy)
			{
				error += dy;
				x0 += sx;
			};

			if (doubled <= dx)
			{
				error += dx;
				y0 += sy;
			};
		};
	};

	void SpriteEditor::Image::Fill(int x, int y, const Colour& colour)
	{
		if (!Contains(x, y))
			return;

		const Colour target = Get(x, y);

		if (target == colour)
			return;

		std::vector<std::pair<int, int>> pending = { { x, y } };

		while (!pending.empty())
		{
			const auto [px, py] = pending.back();
			pending.pop_back();

			if (!Contains(px, py) || Get(px, py) != target)
				continue;

			Set(px, py, colour);

			pending.insert(pending.end(), { { px + 1, py }, { px - 1, py }, { px, py + 1 }, { px, py - 1 } });
		};
	};

	bool SpriteEditor::Load(const std::string& path, Image& image)
	{
		try
		{
			image.pixels = Texture::Decode(path, image.width, image.height);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Sprites: " << e.what() << std::endl;
			return false;
		};

		// Texture's rows run bottom up, the way the renderer uploads them.
		FlipRows(image);

		return true;
	};

	std::vector<char> SpriteEditor::Encode(const std::string& extension, const Image& image)
	{
		std::vector<char> bytes;

		const auto append = [](void* context, void* data, int size)
		{
			std::vector<char>& out = *static_cast<std::vector<char>*>(context);
			out.insert(out.end(), (const char*)data, (const char*)data + size);
		};

		const int width = image.width;
		const int height = image.height;
		const uint8_t* pixels = image.pixels.data();

		int written = 0;

		if (extension == ".png")
			written = stbi_write_png_to_func(append, &bytes, width, height, channels, pixels, width * channels);
		else if (extension == ".bmp")
			written = stbi_write_bmp_to_func(append, &bytes, width, height, channels, pixels);
		else if (extension == ".tga")
			written = stbi_write_tga_to_func(append, &bytes, width, height, channels, pixels);
		else if (extension == ".jpg" || extension == ".jpeg")
			written = stbi_write_jpg_to_func(append, &bytes, width, height, channels, pixels, jpeg_quality);

		if (!written)
			bytes.clear();

		return bytes;
	};

	bool SpriteEditor::Save(const std::string& path, const Image& image)
	{
		const std::vector<char> bytes = Encode(LowerExtension(path), image);

		// Checked before the file is opened, which empties it.
		if (bytes.empty())
		{
			std::cerr << "Sprites: could not encode " << path << std::endl;
			return false;
		};

		std::ofstream file(path, std::ios::binary);

		if (!file.write(bytes.data(), (std::streamsize)bytes.size()))
		{
			std::cerr << "Sprites: could not save " << path << std::endl;
			return false;
		};

		return true;
	};

	void SpriteEditor::Open(const std::string& path)
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

		std::unique_ptr<Document> document = std::make_unique<Document>();
		document->path = normal;

		if (!Load(normal, document->image))
			return;

		document->saved = document->image;

		m_documents.push_back(std::move(document));
	};

	void SpriteEditor::Relocate(const std::string& from, const std::string& to)
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			document->path = ProjectAssets::Relocate(document->path, from, to);
	};

	std::vector<std::string> SpriteEditor::TakeSaved()
	{
		return std::exchange(m_saved, { });
	};

	void SpriteEditor::Draw(bool* open)
	{
		if (m_focusFrames > 0)
		{
			m_focusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_focused = false;

		// The wheel zooms the canvas rather than scrolling the panel.
		if (ImGui::Begin(EditorPanels::Sprites, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (m_documents.empty())
				ImGui::TextDisabled("Click an image in the Project panel to edit it here.");
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
					const std::string label = std::string(EditorIcons::Texture) + " " + std::filesystem::path(document->path).filename().string() + "###" + std::to_string((uintptr_t)document.get());
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

	void SpriteEditor::DrawDocument(Document& document)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
			SaveDocument(document);

		// Asked every frame, mid-stroke too: ImGui only routes a shortcut to a
		// window that asked for it the frame before.
		const bool undo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z);
		const bool redo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);

		if (!document.stroking && (undo || redo))
			Step(document, undo);

		DrawToolbar(document);

		const float status_height = ImGui::GetTextLineHeightWithSpacing();
		const ImVec2 available = ImGui::GetContentRegionAvail();

		DrawCanvas(document, ImVec2(available.x, available.y - status_height));
	};

	void SpriteEditor::DrawToolbar(Document& document)
	{
		for (const ToolInfo& info : tools)
		{
			if (ImGui::Shortcut(info.key))
				m_tool = info.tool;

			if (ImGui::RadioButton(info.label, m_tool == info.tool))
				m_tool = info.tool;

			ImGui::SetItemTooltip("%s (%s)", info.label, ImGui::GetKeyName(info.key));
			ImGui::SameLine();
		};

		float colour[channels];

		for (int i = 0; i < channels; i++)
			colour[i] = m_colour[i] / (float)UINT8_MAX;

		if (ImGui::ColorEdit4("##colour", colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
			for (int i = 0; i < channels; i++)
				m_colour[i] = (uint8_t)std::lround(colour[i] * UINT8_MAX);

		ImGui::SameLine();
		ImGui::Checkbox("Grid", &m_grid);

		ImGui::SameLine();

		// Fitted again once the canvas is next drawn, at its size then.
		if (ImGui::Button("Fit"))
			document.zoom = 0.0f;
	};

	void SpriteEditor::DrawCanvas(Document& document, const ImVec2& size)
	{
		if (size.x < 1.0f || size.y < 1.0f)
			return;

		const Image& image = document.image;
		const ImVec2 corner = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

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

		Paint(document, mouse_x, mouse_y, hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left));

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

		draw->AddRect(origin, image_end, border_colour);

		const bool on_image = hovered && image.Contains(mouse_x, mouse_y);

		if (on_image)
			draw->AddRect(
				ImVec2(origin.x + mouse_x * zoom, origin.y + mouse_y * zoom),
				ImVec2(origin.x + (mouse_x + 1) * zoom, origin.y + (mouse_y + 1) * zoom),
				hover_colour);

		draw->PopClipRect();

		ImGui::TextDisabled("%d x %d, %g%%", image.width, image.height, zoom * 100.0f);

		if (on_image)
		{
			const Colour colour = image.Get(mouse_x, mouse_y);

			ImGui::SameLine();
			ImGui::TextDisabled("  %d, %d  #%02X%02X%02X%02X", mouse_x, mouse_y, colour[0], colour[1], colour[2], colour[3]);
		};
	};

	void SpriteEditor::Paint(Document& document, int x, int y, bool pressed)
	{
		Image& image = document.image;

		// Before the tool is looked at, since it may have changed mid-stroke.
		// A stroke that changed nothing, such as one wholly off the image,
		// leaves nothing to undo.
		if (document.stroking && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			document.stroking = false;

			if (image == document.undo.back())
				document.undo.pop_back();

			document.unsaved = image != document.saved;
			return;
		};

		switch (m_tool)
		{
		case Tool::Picker:
			if (pressed && image.Contains(x, y))
			{
				m_colour = image.Get(x, y);
				m_tool = Tool::Pencil;
			};
			return;

		case Tool::Fill:
			if (pressed && image.Contains(x, y) && image.Get(x, y) != m_colour)
			{
				Remember(document);
				image.Fill(x, y, m_colour);
				document.unsaved = image != document.saved;
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
			document.lastX = x;
			document.lastY = y;
		};

		if (!document.stroking)
			return;

		const Colour colour = m_tool == Tool::Eraser ? Colour{ } : m_colour;

		image.Line(document.lastX, document.lastY, x, y, colour);
		document.lastX = x;
		document.lastY = y;
		document.unsaved = true;
	};

	void SpriteEditor::Remember(Document& document)
	{
		const size_t bytes = (std::max)(document.image.pixels.size(), (size_t)1);
		const size_t kept = (std::max)(max_undo_bytes / bytes, (size_t)1);

		document.undo.push_back(document.image);
		document.redo.clear();

		if (document.undo.size() > kept)
			document.undo.erase(document.undo.begin(), document.undo.end() - (ptrdiff_t)kept);
	};

	void SpriteEditor::Step(Document& document, bool undo)
	{
		std::vector<Image>& from = undo ? document.undo : document.redo;
		std::vector<Image>& to = undo ? document.redo : document.undo;

		if (from.empty())
			return;

		to.push_back(std::move(document.image));
		document.image = std::move(from.back());
		from.pop_back();

		document.unsaved = document.image != document.saved;
	};

	void SpriteEditor::Fit(Document& document, const ImVec2& size)
	{
		const Image& image = document.image;

		constexpr float half = 0.5f;

		float zoom = (std::min)(size.x / image.width, size.y / image.height) * fit_share;

		if (zoom >= 1.0f)
			zoom = std::floor(zoom);

		document.zoom = std::clamp(zoom, min_zoom, max_zoom);
		document.offset = ImVec2(
			std::floor((size.x - image.width * document.zoom) * half),
			std::floor((size.y - image.height * document.zoom) * half));
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

	void SpriteEditor::SaveAll()
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->unsaved)
				SaveDocument(*document);
	};

	bool SpriteEditor::SaveDocument(Document& document)
	{
		if (!Save(document.path, document.image))
			return false;

		document.saved = document.image;
		document.unsaved = false;
		m_saved.push_back(document.path);

		return true;
	};

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

		ImGui::Text("Save the changes to %s?", std::filesystem::path(m_closing->path).filename().string().c_str());

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
		std::erase_if(m_documents, [document](const std::unique_ptr<Document>& open) { return open.get() == document; });
	};
};

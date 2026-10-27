#include "Ui.h"

#include "GameObject.h"
#include "Input.h"
#include "Mesh.h"
#include "Renderer.h"
#include "RichText.h"
#include "Shaders.h"
#include "Textures.h"
#include "Utilities/Clock.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>


namespace Loom
{
	namespace
	{
		#pragma region Drawing

		// Every item is drawn at one depth, nearer than the depth buffer is
		// cleared to, so each lands over the ones before it.
		constexpr float DEPTH = 0.0f;

		constexpr int RGBA = 4;
		constexpr uint8_t OPAQUE = 255;

		const char* const UI_SHADER = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

// The screen's width and height in pixels, in x and y.
uniform vec4 u_screen;

// Clip space runs -1 to 1, so it is twice as wide as 0 to 1.
const float CLIP_SPAN = 2.0;

out vec2 v_uv;

void main()
{
	v_uv = aUV;
	gl_Position = vec4(aPos.x / u_screen.x * CLIP_SPAN - 1.0, 1.0 - aPos.y / u_screen.y * CLIP_SPAN, aPos.z, 1.0);
}

// ===FRAGMENT===

in vec2 v_uv;

uniform sampler2D u_texture;
uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = texture(u_texture, v_uv) * u_color;
}
)";

		// Shared by everything the UI draws and kept until the process ends,
		// like the sprites'. 0 when it will not compile, which is reported once.
		uint32_t UiProgram()
		{
			static const uint32_t program = []() -> uint32_t
			{
				try
				{
					std::istringstream stream(UI_SHADER);
					return (new Shader("Loom UI", stream))->id;
				}
				catch (const std::exception& e)
				{
					std::cerr << "Ui: " << e.what() << std::endl;
					return 0;
				};
			}();

			return program;
		};

		// What a rectangle samples, so it is drawn by the program text is.
		uint32_t WhiteTexture()
		{
			static const uint32_t texture = []() -> uint32_t
			{
				const uint8_t white[RGBA] = { OPAQUE, OPAQUE, OPAQUE, OPAQUE };
				return Renderer::Get()->CreateTexture(1, 1, white);
			}();

			return texture;
		};

		// The image a rect is filled through: its own, or plain white while
		// it has none to draw with.
		uint32_t ImageOf(const UiRect& rect)
		{
			const uint32_t handle = Texture::HandleOf(rect.imagePath);

			return handle != 0 ? handle : WhiteTexture();
		};

		// x, y, z, u, v for each corner of a quad's two triangles.
		void AddQuad(std::vector<float>& vertices, glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax)
		{
			vertices.insert(vertices.end(),
			{
				min.x, min.y, DEPTH, uvMin.x, uvMin.y,
				max.x, min.y, DEPTH, uvMax.x, uvMin.y,
				max.x, max.y, DEPTH, uvMax.x, uvMax.y,
				min.x, min.y, DEPTH, uvMin.x, uvMin.y,
				max.x, max.y, DEPTH, uvMax.x, uvMax.y,
				min.x, max.y, DEPTH, uvMin.x, uvMax.y,
			});
		};

		void Draw(uint32_t program, uint32_t texture, const glm::vec4& color, const std::vector<float>& vertices)
		{
			if (vertices.empty() || texture == 0)
				return;

			Renderer* renderer = Renderer::Get();

			renderer->SetTexture(program, "u_texture", texture);
			renderer->SetUniform(program, "u_color", color);
			renderer->Draw(program, Mesh::Triangles, vertices.data(), vertices.size() / FloatsPerVertex(VertexFormat::PositionUV), Mesh::Stream, VertexFormat::PositionUV);
		};

		bool overlay = false;
		std::vector<const ComponentBase*> queued;

		#pragma endregion

		#pragma region Text

		constexpr float HALF = 0.5f;

		// How far across the room beside it each alignment puts a line.
		constexpr float ALIGNED[] = { 0.0f, HALF, 1.0f };

		constexpr int FIRST_CHARACTER = 32;
		constexpr int CHARACTERS = 95;
		constexpr int ATLAS_START = 256;
		constexpr int ATLAS_LARGEST = 4096;

		// The most of a file stb_truetype reads to find a font in it.
		constexpr size_t FONT_HEADER = 16;

		// The printable ASCII characters of one font at one size.
		struct Font
		{
			uint32_t texture = 0;
			int atlasSize = 0;
			float ascent = 0.0f;
			float lineHeight = 0.0f;
			stbtt_bakedchar characters[CHARACTERS]{ };
			unsigned long long lastUsed = 0;
		};

		// Null for a file that will not load or bake, kept so it is reported once.
		std::map<std::pair<std::string, int>, std::unique_ptr<Font>> fonts;

		std::unique_ptr<Font> Bake(const std::string& path, int pixels)
		{
			std::ifstream file(path, std::ios::binary);
			const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

			const int offset = ttf.size() < FONT_HEADER ? -1 : stbtt_GetFontOffsetForIndex(ttf.data(), 0);
			stbtt_fontinfo info;

			if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
			{
				std::cerr << "UiText: no font at " << path << std::endl;
				return nullptr;
			};

			auto font = std::make_unique<Font>();
			std::vector<unsigned char> coverage;

			for (int side = ATLAS_START; side <= ATLAS_LARGEST && font->atlasSize == 0; side *= 2)
			{
				coverage.assign((size_t)side * side, 0);

				if (stbtt_BakeFontBitmap(ttf.data(), offset, (float)pixels, coverage.data(), side, side, FIRST_CHARACTER, CHARACTERS, font->characters) > 0)
					font->atlasSize = side;
			};

			if (font->atlasSize == 0)
			{
				std::cerr << "UiText: " << path << " will not bake at " << pixels << " pixels" << std::endl;
				return nullptr;
			};

			int ascent = 0;
			int descent = 0;
			int gap = 0;
			stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);

			const float scale = stbtt_ScaleForPixelHeight(&info, (float)pixels);
			font->ascent = ascent * scale;
			font->lineHeight = (ascent - descent) * scale;

			// White, with the coverage as alpha, and bottom row first as the
			// renderer takes it, where the baker writes the top row first.
			const int side = font->atlasSize;
			std::vector<uint8_t> rgba((size_t)side * side * RGBA, OPAQUE);

			for (int y = 0; y < side; y++)
				for (int x = 0; x < side; x++)
					rgba[((size_t)(side - 1 - y) * side + x) * RGBA + RGBA - 1] = coverage[(size_t)y * side + x];

			font->texture = Renderer::Get()->CreateTexture(side, side, rgba.data());

			return font;
		};

		// Null without a renderer to hold the atlas.
		Font* FontFor(const std::string& path, float size)
		{
			if (Renderer::Get() == nullptr)
				return nullptr;

			const auto key = std::make_pair(path, std::max((int)std::lround(size), 1));
			auto found = fonts.find(key);

			if (found == fonts.end())
				found = fonts.emplace(key, Bake(key.first, key.second)).first;

			Font* font = found->second.get();

			if (font != nullptr)
				font->lastUsed = Time::FrameCount();

			return font;
		};

		void LetGoOfUnusedFonts()
		{
			const unsigned long long now = Time::FrameCount();

			std::erase_if(fonts, [now](const auto& entry)
			{
				const Font* font = entry.second.get();

				if (font == nullptr || font->lastUsed + UiText::unusedFrames >= now)
					return false;

				Renderer::Get()->DestroyTexture(font->texture);
				return true;
			});
		};

		bool IsPrintable(char character)
		{
			return character >= FIRST_CHARACTER && character < FIRST_CHARACTER + CHARACTERS;
		};

		// One line's run in one font and colour.
		struct Piece
		{
			const Font* font = nullptr;
			glm::vec4 color{ 1.0f };
			std::string text;
		};

		struct Line
		{
			std::vector<Piece> pieces;
			float width = 0.0f;
			float ascent = 0.0f;
			float height = 0.0f;
		};

		constexpr char LINE_BREAK = '\n';
		constexpr char SPACE = ' ';

		float WidthOf(const Font& font, const std::string& text)
		{
			float width = 0.0f;

			for (const char character : text)
				if (IsPrintable(character))
					width += font.characters[character - FIRST_CHARACTER].xadvance;

			return width;
		};

		void Grow(Line& line, const Font& font)
		{
			line.ascent = std::max(line.ascent, font.ascent);
			line.height = std::max(line.height, font.lineHeight);
		};

		void Add(Line& line, const Font& font, const glm::vec4& color, const std::string& text, float width)
		{
			if (line.pieces.empty() || line.pieces.back().font != &font || line.pieces.back().color != color)
				line.pieces.push_back({ &font, color, "" });

			line.pieces.back().text += text;
			line.width += width;

			Grow(line, font);
		};

		// The text cut into lines at its breaks and, with wrap, at the spaces
		// that keep each inside width; the spaces a line is cut at are left
		// out. None when its font will not load, and a span whose size will
		// not load is drawn at the text's own.
		std::vector<Line> LayOut(const UiText& text, float width)
		{
			const Font* const base = FontFor(text.fontPath, text.fontSize);

			if (base == nullptr)
				return { };

			const Font* font = base;

			std::vector<Line> lines(1);

			// Spaces after the last word, added only once the next word is
			// on the same line.
			Piece spaces{ };
			float spacesWidth = 0.0f;

			for (const RichText::Span& span : RichText::Spans(text.text, text.color, text.fontSize))
			{
				font = FontFor(text.fontPath, span.size);

				if (font == nullptr)
					font = base;

				for (size_t at = 0; at < span.text.size(); )
				{
					if (span.text[at++] == LINE_BREAK)
					{
						Grow(lines.back(), *font);
						lines.emplace_back();
						spaces.text.clear();
						spacesWidth = 0.0f;
						continue;
					};

					const bool space = span.text[at - 1] == SPACE;
					size_t end = at;

					while (end < span.text.size() && span.text[end] != LINE_BREAK && (span.text[end] == SPACE) == space)
						end++;

					const std::string chunk = span.text.substr(at - 1, end - at + 1);
					const float chunkWidth = WidthOf(*font, chunk);

					at = end;

					if (space)
					{
						spaces = { font, span.color, spaces.text + chunk };
						spacesWidth += chunkWidth;
						continue;
					};

					if (text.wrap && !lines.back().pieces.empty() && lines.back().width + spacesWidth + chunkWidth > width)
						lines.emplace_back();
					else if (!spaces.text.empty())
						Add(lines.back(), *spaces.font, spaces.color, spaces.text, spacesWidth);

					spaces.text.clear();
					spacesWidth = 0.0f;
					Add(lines.back(), *font, span.color, chunk, chunkWidth);
				};
			};

			// An empty last line is as high as the font it ends in.
			Grow(lines.back(), *font);

			return lines;
		};

		glm::vec2 SizeOf(const std::vector<Line>& lines)
		{
			glm::vec2 size(0.0f);

			for (const Line& line : lines)
			{
				size.x = std::max(size.x, line.width);
				size.y += line.height;
			};

			return size;
		};

		void DrawText(uint32_t program, const UiText& text, std::vector<float>& vertices)
		{
			const Ui::Rect within = text.Area();
			const std::vector<Line> lines = LayOut(text, within.size.x);
			const int alignment = std::clamp((int)*text.alignment, (int)UiText::Left, (int)UiText::Right);
			const int vertical = std::clamp((int)*text.verticalAlignment, (int)UiText::Top, (int)UiText::Bottom);

			float top = within.min.y + (within.size.y - SizeOf(lines).y) * ALIGNED[vertical];

			for (const Line& line : lines)
			{
				float x = within.min.x + (within.size.x - line.width) * ALIGNED[alignment];
				float y = top + line.ascent;

				for (const Piece& piece : line.pieces)
				{
					vertices.clear();

					for (const char character : piece.text)
					{
						if (!IsPrintable(character))
							continue;

						stbtt_aligned_quad quad;
						stbtt_GetBakedQuad(piece.font->characters, piece.font->atlasSize, piece.font->atlasSize, character - FIRST_CHARACTER, &x, &y, &quad, 1);

						// The baker's t runs down from the atlas's top; the texture's v up from its bottom.
						AddQuad(vertices, { quad.x0, quad.y0 }, { quad.x1, quad.y1 }, { quad.s0, 1.0f - quad.t0 }, { quad.s1, 1.0f - quad.t1 });
					};

					Draw(program, piece.font->texture, piece.color, vertices);
				};

				top += line.height;
			};
		};

		#pragma endregion

		#pragma region Pointer

		constexpr int LEFT_BUTTON = 0;

		// The rects that block the pointer the last overlay drew, in the order
		// drawn, and the buttons they press, by their IDs, which are never
		// handed out again.
		struct Hit
		{
			uint64_t rect;
			std::optional<uint64_t> button;
			Ui::Rect where;
		};

		std::vector<Hit> hits;
		unsigned long long hitsFrame = 0;
		bool hitsDrawn = false;

		// The hit the cursor is on, or null.
		const Hit* Pointed()
		{
			if (!hitsDrawn || hitsFrame + 1 < Time::FrameCount())
				return nullptr;

			const glm::vec2 pointer = Ui::Pointer();

			for (auto hit = hits.rbegin(); hit != hits.rend(); hit++)
				if (hit->where.Contains(pointer))
					return &*hit;

			return nullptr;
		};

		#pragma endregion
	};

	#pragma region Pointer

	Ui::Rect Ui::Screen()
	{
		return { glm::vec2(0.0f), glm::vec2((float)Input::screen_width, (float)Input::screen_height) };
	};

	glm::vec2 Ui::Pointer()
	{
		return glm::vec2((float)Input::mouse_x, (float)Input::mouse_y);
	};

	bool Ui::IsPointerOver()
	{
		return Pointed() != nullptr;
	};

	bool Ui::IsPointedAt(const UiRect& rect)
	{
		const Hit* hit = Pointed();

		return hit != nullptr && hit->rect == rect.m_ID;
	};

	#pragma endregion

	#pragma region Overlay

	void Ui::BeginOverlay()
	{
		overlay = true;
		queued.clear();
	};

	void Ui::Queue(const ComponentBase* component)
	{
		if (overlay)
			queued.push_back(component);
	};

	void Ui::DrawOverlay()
	{
		overlay = false;

		// Laid aside until everything is drawn, as buttons are tinted from the
		// last overlay's.
		std::vector<Hit> drawn;

		const Rect screen = Screen();
		Renderer* renderer = Renderer::Get();
		const uint32_t program = queued.empty() || renderer == nullptr ? 0 : UiProgram();

		if (program != 0 && screen.size.x > 0.0f && screen.size.y > 0.0f)
		{
			renderer->SetUniform(program, "u_screen", glm::vec4(screen.size, 0.0f, 0.0f));
			renderer->Clear(nullptr, true);
			renderer->SetDepthTestEqual(true);

			std::vector<float> vertices;

			for (const ComponentBase* component : queued)
			{
				vertices.clear();

				if (const UiRect* rect = dynamic_cast<const UiRect*>(component))
				{
					const Rect where = rect->ScreenRect();
					const glm::vec4 fill = rect->Fill();

					if (rect->blocksPointer)
					{
						const UiButton* button = rect->Button();

						drawn.push_back({ rect->m_ID, button ? std::optional<uint64_t>(button->m_ID) : std::nullopt, where });
					};

					if (fill.a <= 0.0f)
						continue;

					// Images are bottom row first, so the top samples the higher v.
					AddQuad(vertices, where.min, where.min + where.size, glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f));
					Draw(program, ImageOf(*rect), fill, vertices);
				}
				else if (const UiText* text = dynamic_cast<const UiText*>(component))
					DrawText(program, *text, vertices);
			};

			renderer->SetDepthTestEqual(false);
		};

		hits = std::move(drawn);
		hitsFrame = Time::FrameCount();
		hitsDrawn = true;

		queued.clear();

		if (renderer != nullptr)
			LetGoOfUnusedFonts();
	};

	#pragma endregion

	#pragma region UiRect

	Ui::Rect UiRect::LayOut(const Ui::Rect& parent) const
	{
		const glm::vec2 anchored = parent.min + parent.size * *anchor;

		return { anchored + *offset - *size * *pivot, *size };
	};

	Ui::Rect UiRect::ScreenRect() const
	{
		GameObject* parent = m_gameObject->GetParent();
		const UiRect* outer = parent ? parent->GetComponent<UiRect>() : nullptr;

		return LayOut(outer ? outer->ScreenRect() : Ui::Screen());
	};

	const UiButton* UiRect::Button() const
	{
		for (GameObject* object = m_gameObject; object != nullptr; object = object->GetParent())
			if (const UiButton* button = object->GetComponent<UiButton>())
				return button;

		return nullptr;
	};

	glm::vec4 UiRect::Fill() const
	{
		const UiButton* button = m_gameObject->GetComponent<UiButton>();

		return button ? button->Tint() : *color;
	};

	void UiRect::OnRender()
	{
		Ui::Queue(this);

		if (const UiText* text = m_gameObject->GetComponent<UiText>())
			Ui::Queue(text);
	};

	#pragma endregion

	#pragma region UiText

	glm::vec2 UiText::Measure() const
	{
		return SizeOf(LayOut(*this, Area().size.x));
	};

	Ui::Rect UiText::Area() const
	{
		const UiRect* box = m_gameObject->GetComponent<UiRect>();

		return box ? box->ScreenRect() : Ui::Screen();
	};

	void UiText::OnRender()
	{
		// One beside a UiRect is queued by it, so it is drawn over it.
		if (m_gameObject->GetComponent<UiRect>() == nullptr)
			Ui::Queue(this);
	};

	#pragma endregion

	#pragma region UiButton

	bool UiButton::IsHovered() const
	{
		const Hit* hit = Pointed();

		return hit != nullptr && hit->button == m_ID;
	};

	bool UiButton::WasClicked() const
	{
		return IsHovered() && Input::GetMouseButtonDown(LEFT_BUTTON);
	};

	glm::vec4 UiButton::Tint() const
	{
		if (!IsHovered())
			return normalColor;

		return Input::GetMouseButton(LEFT_BUTTON) ? *pressedColor : *hoverColor;
	};

	void UiButton::OnUpdate()
	{
		if (onClick && WasClicked())
			onClick();
	};

	#pragma endregion
};

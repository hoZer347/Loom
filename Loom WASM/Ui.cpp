#include "Ui.h"

#include "GameObject.h"
#include "Input.h"
#include "Mesh.h"
#include "Renderer.h"
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
#include <set>
#include <sstream>
#include <utility>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr int LEFT_BUTTON = 0;

		// Every item is drawn at one depth, nearer than the depth buffer is
		// cleared to, so each lands over the ones before it.
		constexpr float DEPTH = 0.0f;

		constexpr float HALF = 0.5f;

		// How far across the room beside it each alignment puts a line.
		constexpr float ALIGNED[] = { 0.0f, HALF, 1.0f };

		constexpr int FIRST_CHARACTER = 32;
		constexpr int CHARACTERS = 95;
		constexpr int ATLAS_START = 256;
		constexpr int ATLAS_LARGEST = 4096;
		constexpr int RGBA = 4;
		constexpr uint8_t OPAQUE = 255;

		// The most of a file stb_truetype reads to find a font in it.
		constexpr size_t FONT_HEADER = 16;

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
		// The image a rect is filled through: its own, or plain white when it
		// names none or one that will not load, which is tried only once.
		uint32_t ImageOf(const UiRect& rect)
		{
			static std::set<std::string> broken;

			const std::string& path = rect.imagePath;

			if (path.empty() || broken.contains(path))
				return WhiteTexture();

			if (const Texture* image = Texture::Shared(path))
				return image->handle;

			broken.insert(path);

			return WhiteTexture();
		};

		uint32_t WhiteTexture()
		{
			static const uint32_t texture = []() -> uint32_t
			{
				const uint8_t white[RGBA] = { OPAQUE, OPAQUE, OPAQUE, OPAQUE };
				return Renderer::Get()->CreateTexture(1, 1, white);
			}();

			return texture;
		};

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

		glm::vec2 MeasureWith(const Font& font, const std::string& text)
		{
			float width = 0.0f;

			for (const char character : text)
				if (IsPrintable(character))
					width += font.characters[character - FIRST_CHARACTER].xadvance;

			return glm::vec2(width, font.lineHeight);
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

		void DrawText(uint32_t program, const UiText& text, std::vector<float>& vertices)
		{
			const Font* font = FontFor(text.fontPath, text.fontSize);

			if (font == nullptr)
				return;

			const Ui::Rect within = text.Area();
			const glm::vec2 measured = MeasureWith(*font, text.text);
			const int alignment = std::clamp((int)*text.alignment, (int)UiText::Left, (int)UiText::Right);

			float x = within.min.x + (within.size.x - measured.x) * ALIGNED[alignment];
			float y = within.min.y + (within.size.y - font->lineHeight) * HALF + font->ascent;

			for (const char character : *text.text)
			{
				if (!IsPrintable(character))
					continue;

				stbtt_aligned_quad quad;
				stbtt_GetBakedQuad(font->characters, font->atlasSize, font->atlasSize, character - FIRST_CHARACTER, &x, &y, &quad, 1);

				// The baker's t runs down from the atlas's top; the texture's v up from its bottom.
				AddQuad(vertices, { quad.x0, quad.y0 }, { quad.x1, quad.y1 }, { quad.s0, 1.0f - quad.t0 }, { quad.s1, 1.0f - quad.t1 });
			};

			Draw(program, font->texture, text.color, vertices);
		};
	};

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

	glm::vec2 UiText::Measure() const
	{
		const Font* font = FontFor(fontPath, fontSize);

		return font ? MeasureWith(*font, text) : glm::vec2(0.0f);
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
};

#include "Ui.h"

#include "GameObject.h"
#include "Input.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Utilities/Clock.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr uint32_t STREAM_DRAW = 0x88E0;	// GL_STREAM_DRAW
		constexpr int LEFT_BUTTON = 0;

		// In front of the whole scene, but behind clip space's near plane at
		// -1, which clips a quad lying on it; and how much nearer each item
		// drawn over the last one is, so they stack in the order queued.
		constexpr float FRONT = -0.99f;
		constexpr float DEPTH_STEP = 1e-5f;

		constexpr int FIRST_CHARACTER = 32;
		constexpr int CHARACTERS = 95;
		constexpr int ATLAS_START = 256;
		constexpr int ATLAS_LARGEST = 4096;
		constexpr int RGBA = 4;
		constexpr uint8_t OPAQUE = 255;

		const char* const UI_SHADER = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

// The screen's width and height in pixels, in x and y.
uniform vec4 u_screen;

out vec2 v_uv;

void main()
{
	v_uv = aUV;
	gl_Position = vec4(aPos.x / u_screen.x * 2.0 - 1.0, 1.0 - aPos.y / u_screen.y * 2.0, aPos.z, 1.0);
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
				Renderer* renderer = Renderer::Get();
				return renderer ? renderer->CreateTexture(1, 1, white) : 0;
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
		};

		// Null when the file will not load or bake, which is reported once.
		const Font* FontFor(const std::string& path, float size)
		{
			static std::map<std::pair<std::string, int>, std::unique_ptr<Font>> fonts;

			const int pixels = std::max((int)std::lround(size), 1);
			const auto key = std::make_pair(path, pixels);

			if (const auto found = fonts.find(key); found != fonts.end())
				return found->second.get();

			std::unique_ptr<Font>& font = fonts[key];

			std::ifstream file(path, std::ios::binary);
			const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			stbtt_fontinfo info;

			if (ttf.empty() || !stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
			{
				std::cerr << "UiText: no font at " << path << std::endl;
				return nullptr;
			};

			auto baked = std::make_unique<Font>();
			std::vector<unsigned char> coverage;

			for (int side = ATLAS_START; side <= ATLAS_LARGEST && baked->atlasSize == 0; side *= 2)
			{
				coverage.assign((size_t)side * side, 0);

				if (stbtt_BakeFontBitmap(ttf.data(), 0, (float)pixels, coverage.data(), side, side, FIRST_CHARACTER, CHARACTERS, baked->characters) > 0)
					baked->atlasSize = side;
			};

			Renderer* renderer = Renderer::Get();

			if (baked->atlasSize == 0 || renderer == nullptr)
			{
				std::cerr << "UiText: " << path << " will not bake at " << pixels << " pixels" << std::endl;
				return nullptr;
			};

			int ascent = 0;
			int descent = 0;
			int gap = 0;
			stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);

			const float scale = stbtt_ScaleForPixelHeight(&info, (float)pixels);
			baked->ascent = ascent * scale;
			baked->lineHeight = (ascent - descent) * scale;

			// White, with the coverage as alpha, and bottom row first as the
			// renderer takes it, where the baker writes the top row first.
			const int side = baked->atlasSize;
			std::vector<uint8_t> rgba((size_t)side * side * RGBA, OPAQUE);

			for (int y = 0; y < side; y++)
				for (int x = 0; x < side; x++)
					rgba[((size_t)(side - 1 - y) * side + x) * RGBA + RGBA - 1] = coverage[(size_t)y * side + x];

			baked->texture = renderer->CreateTexture(side, side, rgba.data());
			font = std::move(baked);

			return font.get();
		};

		// x, y, z, u, v for each corner of a quad's two triangles.
		void AddQuad(std::vector<float>& vertices, glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax, float depth)
		{
			vertices.insert(vertices.end(),
			{
				min.x, min.y, depth, uvMin.x, uvMin.y,
				max.x, min.y, depth, uvMax.x, uvMin.y,
				max.x, max.y, depth, uvMax.x, uvMax.y,
				min.x, min.y, depth, uvMin.x, uvMin.y,
				max.x, max.y, depth, uvMax.x, uvMax.y,
				min.x, max.y, depth, uvMin.x, uvMax.y,
			});
		};

		void Draw(uint32_t program, uint32_t texture, const glm::vec4& color, const std::vector<float>& vertices)
		{
			Renderer* renderer = Renderer::Get();

			if (vertices.empty() || texture == 0)
				return;

			renderer->SetTexture(program, "u_texture", texture);
			renderer->SetUniform(program, "u_color", color);
			renderer->Draw(program, Mesh::Triangles, vertices.data(), vertices.size() / FloatsPerVertex(VertexFormat::PositionUV), STREAM_DRAW, VertexFormat::PositionUV);
		};

		bool overlay = false;
		std::vector<const ComponentBase*> queued;

		// The last frame the cursor was over a UiRect that blocks it.
		unsigned long long pointerOverFrame = 0;
		bool pointerEverOver = false;
	};

	#pragma region Ui

	bool Ui::Rect::Contains(const glm::vec2& point) const
	{
		return point.x >= min.x && point.y >= min.y && point.x < min.x + size.x && point.y < min.y + size.y;
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
		return pointerEverOver && pointerOverFrame + 1 >= Time::FrameCount();
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

		const Rect screen = Screen();

		if (queued.empty() || screen.size.x <= 0.0f || screen.size.y <= 0.0f || Renderer::Get() == nullptr)
			return;

		const uint32_t program = UiProgram();

		if (program == 0)
			return;

		Renderer::Get()->SetUniform(program, "u_screen", glm::vec4(screen.size, 0.0f, 0.0f));

		float depth = FRONT + DEPTH_STEP * (float)(queued.size() + 1);

		for (const ComponentBase* component : queued)
		{
			depth -= DEPTH_STEP;

			if (const UiRect* rect = dynamic_cast<const UiRect*>(component))
			{
				const Rect where = rect->ScreenRect();
				std::vector<float> vertices;

				AddQuad(vertices, where.min, where.min + where.size, glm::vec2(0.0f), glm::vec2(1.0f), depth);
				Draw(program, WhiteTexture(), rect->color, vertices);
			}
			else if (const UiText* text = dynamic_cast<const UiText*>(component))
			{
				const Font* font = FontFor(text->fontPath, text->fontSize);

				if (font == nullptr)
					continue;

				const Rect within = text->Area();
				const glm::vec2 measured = text->Measure();

				float x = within.min.x;

				if (text->alignment == UiText::Centre)
					x += (within.size.x - measured.x) / 2.0f;
				else if (text->alignment == UiText::Right)
					x += within.size.x - measured.x;

				float y = within.min.y + (within.size.y - font->lineHeight) / 2.0f + font->ascent;
				std::vector<float> vertices;

				for (const char character : *text->text)
				{
					if (character < FIRST_CHARACTER || character >= FIRST_CHARACTER + CHARACTERS)
						continue;

					stbtt_aligned_quad quad;
					stbtt_GetBakedQuad(font->characters, font->atlasSize, font->atlasSize, character - FIRST_CHARACTER, &x, &y, &quad, 1);

					// The baker's t runs down from the atlas's top; the texture's v up from its bottom.
					AddQuad(vertices, { quad.x0, quad.y0 }, { quad.x1, quad.y1 }, { quad.s0, 1.0f - quad.t0 }, { quad.s1, 1.0f - quad.t1 }, depth);
				};

				Draw(program, font->texture, text->color, vertices);
			};
		};

		queued.clear();
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

	void UiRect::OnUpdate()
	{
		if (blocksPointer && ScreenRect().Contains(Ui::Pointer()))
		{
			pointerOverFrame = Time::FrameCount();
			pointerEverOver = true;
		};
	};

	void UiRect::OnRender()
	{
		Ui::Queue(this);
	};

	#pragma endregion

	#pragma region UiText

	glm::vec2 UiText::Measure() const
	{
		const Font* font = FontFor(fontPath, fontSize);

		if (font == nullptr)
			return glm::vec2(0.0f);

		float width = 0.0f;

		for (const char character : *text)
			if (character >= FIRST_CHARACTER && character < FIRST_CHARACTER + CHARACTERS)
				width += font->characters[character - FIRST_CHARACTER].xadvance;

		return glm::vec2(width, font->lineHeight);
	};

	Ui::Rect UiText::Area() const
	{
		const UiRect* box = m_gameObject->GetComponent<UiRect>();

		return box ? box->ScreenRect() : Ui::Screen();
	};

	void UiText::OnRender()
	{
		Ui::Queue(this);
	};

	#pragma endregion

	#pragma region UiButton

	void UiButton::OnUpdate()
	{
		UiRect* rect = m_gameObject->GetComponent<UiRect>();

		m_hovered = rect != nullptr && rect->ScreenRect().Contains(Ui::Pointer());
		m_clicked = m_hovered && Input::GetMouseButtonDown(LEFT_BUTTON);

		if (rect != nullptr)
			rect->color = m_hovered && Input::GetMouseButton(LEFT_BUTTON) ? *pressedColor : m_hovered ? *hoverColor : *normalColor;

		if (m_clicked && onClick)
			onClick();
	};

	#pragma endregion
};

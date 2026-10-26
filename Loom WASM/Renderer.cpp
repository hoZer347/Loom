#include "Renderer.h"

#include <algorithm>
#include <charconv>


namespace Loom
{
	size_t FloatsPerVertex(VertexFormat format)
	{
		constexpr size_t POSITION = 3;
		constexpr size_t UV = 2;
		constexpr size_t NORMAL = 3;
		constexpr size_t COLOR = 4;

		switch (format)
		{
		case VertexFormat::PositionUV:
			return POSITION + UV;
		case VertexFormat::PositionUVNormal:
			return POSITION + UV + NORMAL;
		case VertexFormat::PositionUVNormalColor:
			return POSITION + UV + NORMAL + COLOR;
		default:
			return POSITION;
		};
	};

	size_t FloatsPerVertex(const VertexLayout& layout)
	{
		size_t floats = FloatsPerVertex(layout.format);

		for (const UVLayout& layer : layout.layers)
			floats += layer.words;

		return floats;
	};

	std::optional<UVSlot> ParseUVSlot(std::string_view attribute)
	{
		constexpr std::string_view PREFIX = "loom_UV_";

		if (!attribute.starts_with(PREFIX))
			return std::nullopt;

		const char* const end = attribute.data() + attribute.size();
		UVSlot slot;

		const auto [afterLayer, layerError] = std::from_chars(attribute.data() + PREFIX.size(), end, slot.layer);

		if (layerError != std::errc() || afterLayer == end || *afterLayer != '_')
			return std::nullopt;

		const auto [afterSlot, slotError] = std::from_chars(afterLayer + 1, end, slot.slot);

		if (slotError != std::errc() || afterSlot != end)
			return std::nullopt;

		return slot;
	};

	UVSource SourceOf(const VertexLayout& layout, UVSlot slot)
	{
		size_t offset = FloatsPerVertex(layout.format);

		for (const UVLayout& layer : layout.layers)
		{
			if (layer.id == slot.layer)
			{
				const uint32_t first = slot.slot * Renderer::WordsPerUVLocation;

				if (first >= layer.words)
					return { };

				return { offset + first, std::min(layer.words - first, Renderer::WordsPerUVLocation) };
			};

			offset += layer.words;
		};

		return { };
	};

	std::unique_ptr<Renderer> CreateOpenGLRenderer();
#ifndef __EMSCRIPTEN__
	std::unique_ptr<Renderer> CreateVulkanRenderer();
#endif

	std::unique_ptr<Renderer> Renderer::Create(Backend backend)
	{
#ifndef __EMSCRIPTEN__
		if (backend == Backend::Vulkan)
			return CreateVulkanRenderer();
#endif

		return CreateOpenGLRenderer();
	};
};

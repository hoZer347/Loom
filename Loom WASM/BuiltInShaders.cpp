#include "BuiltInShaders.h"

#include "Shaders.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string_view>


namespace Loom
{
	#pragma region Sources

	namespace
	{
		constexpr char LIT_SETTINGS[] = R"(// ===SETTINGS===

shading = on
shadows = on
)";

		constexpr char UNLIT_SETTINGS[] = R"(// ===SETTINGS===

shading = off
shadows = off
)";

		constexpr char TRANSPARENT_SETTINGS[] = R"(// ===SETTINGS===

shading = on
shadows = off
transparency = on
)";

		constexpr char GLOW_SETTINGS[] = R"(// ===SETTINGS===

shading = off
shadows = on
)";

		constexpr char COLOR_VERTEX[] = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		constexpr char TEXTURE_VERTEX[] = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_uv;

void main()
{
	v_uv = aUV;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		constexpr char COLOR_FRAGMENT[] = R"(
// ===FRAGMENT===

uniform vec3 u_color;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0);
}
)";

		constexpr char TEXTURE_FRAGMENT[] = R"(
// ===FRAGMENT===

uniform vec3 u_color;
uniform sampler2D u_texture;

in vec2 v_uv;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0) * texture(u_texture, v_uv);
}
)";

		constexpr char TRANSPARENT_FRAGMENT[] = R"(
// ===FRAGMENT===

uniform vec3 u_color;
uniform float u_transparency;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0 - u_transparency);
}
)";

		constexpr char GLOW_FRAGMENT[] = R"(
// ===FRAGMENT===

uniform vec3 u_color;

out vec4 FragColor;

// Just under solid, the mark Loom/Bloom.loompixel spreads.
const float ALPHA = 0.98;

void main()
{
	FragColor = vec4(u_color, ALPHA);
}
)";

		constexpr char BLOOM_PASS[] = R"(// Spreads what Loom/Glow.shader drew over the pixels around it. Colours clip at 1,
// so brightness cannot tell glow from a sunlit face: Loom/Glow.shader's alpha does.

out vec4 FragColor;

// What Loom/Glow.shader's ALPHA leaves under 1 once blended and stored in 8 bits.
const float GLOW_MARK = 0.0196;

// Past this much of the mark, a pixel is see-through rather than glowing.
const float MOST_COVERED = 1.5;

// Thin lights fill little of the taps, so the tint climbs fast and levels off rather than clipping.
const float STRENGTH = 9.0;

// Taps sit between four pixels, two apart, so none are skipped.
const int STEPS = 5;
const float TAP_SPACING = 2.0;
const float TAP_CORNER = 1.0;
const float FALLOFF = 2.5;

float Covered(float alpha)
{
	float covered = (1.0 - alpha) / GLOW_MARK;

	return covered <= MOST_COVERED ? min(covered, 1.0) : 0.0;
}

void main()
{
	ivec2 pixel = LoomScreenPixel();
	vec4 scene = LoomScenePixel(pixel);

	if (Covered(scene.a) > 0.0)
	{
		FragColor = scene;
		return;
	}

	vec2 size = vec2(textureSize(u_scene, 0));
	vec3 glow = vec3(0.0);
	float covered = 0.0;
	float total = 0.0;

	for (int y = -STEPS; y <= STEPS; y++)
		for (int x = -STEPS; x <= STEPS; x++)
		{
			vec2 taps = vec2(x, y);
			vec2 reached = taps / float(STEPS);
			float reach = dot(reached, reached);

			if (reach > 1.0)
				continue;

			float weight = exp(-reach * FALLOFF);
			vec4 tap = texture(u_scene, (vec2(pixel) + TAP_CORNER + taps * TAP_SPACING) / size);

			float share = Covered(tap.a) * weight;

			glow += tap.rgb * share;
			covered += share;
			total += weight;
		}

	FragColor = covered > 0.0 ? vec4(mix(scene.rgb, glow / covered, 1.0 - exp(-covered / total * STRENGTH)), 1.0) : scene;
}
)";

		struct Entry
		{
			std::string path;
			std::string source;
		};

		std::string Joined(const char* settings, const char* vertex, const char* fragment)
		{
			return std::string(settings) + vertex + fragment;
		};

		const std::vector<Entry>& Entries()
		{
			static const std::vector<Entry> entries =
			{
				{ "Loom/Lit.shader", Joined(LIT_SETTINGS, COLOR_VERTEX, COLOR_FRAGMENT) },
				{ "Loom/Unlit.shader", Joined(UNLIT_SETTINGS, COLOR_VERTEX, COLOR_FRAGMENT) },
				{ "Loom/LitTextured.shader", Joined(LIT_SETTINGS, TEXTURE_VERTEX, TEXTURE_FRAGMENT) },
				{ "Loom/UnlitTextured.shader", Joined(UNLIT_SETTINGS, TEXTURE_VERTEX, TEXTURE_FRAGMENT) },
				{ "Loom/Transparent.shader", Joined(TRANSPARENT_SETTINGS, COLOR_VERTEX, TRANSPARENT_FRAGMENT) },
				{ "Loom/Glow.shader", Joined(GLOW_SETTINGS, COLOR_VERTEX, GLOW_FRAGMENT) },
				{ "Loom/Bloom.loompixel", BLOOM_PASS },
			};

			return entries;
		};
	};

	#pragma endregion

	#pragma region Queries

	const std::vector<std::string>& BuiltInShaders::Paths()
	{
		static const std::vector<std::string> paths = []
			{
				std::vector<std::string> all{ };

				std::transform(Entries().begin(), Entries().end(), std::back_inserter(all), [](const Entry& entry) { return entry.path; });

				return all;
			}();

		return paths;
	};

	std::vector<std::string> BuiltInShaders::Paths(const std::string& extension)
	{
		std::vector<std::string> found{ };

		std::copy_if(Paths().begin(), Paths().end(), std::back_inserter(found), [&extension](const std::string& path) { return path.ends_with(extension); });

		return found;
	};

	const char* BuiltInShaders::SourceOf(const std::string& path)
	{
		const std::string key = Shader::SourceKey(path);

		const auto found = std::find_if(Entries().begin(), Entries().end(), [&key](const Entry& entry) { return entry.path == key; });

		return found != Entries().end() ? found->source.c_str() : nullptr;
	};

	std::string BuiltInShaders::Resolved(const std::string& path)
	{
		static constexpr std::string_view PROJECT_FOLDER = "Assets/";
		static constexpr std::string_view ENGINE_FOLDER = "Loom/";

		const std::string key = Shader::SourceKey(path);

		if (!key.starts_with(PROJECT_FOLDER) || key.find('/', PROJECT_FOLDER.size()) != std::string::npos)
			return path;

		const std::string builtIn = std::string(ENGINE_FOLDER) + key.substr(PROJECT_FOLDER.size());

		if (SourceOf(builtIn) == nullptr || Shader::SourceFor(key).has_value())
			return path;

		std::error_code code;

		return std::filesystem::exists(key, code) ? path : builtIn;
	};

	#pragma endregion
};

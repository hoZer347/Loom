#include "BuiltInShaders.h"

#include "Shaders.h"

#include <algorithm>
#include <iterator>


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

	const char* BuiltInShaders::SourceOf(const std::string& path)
	{
		const std::string key = Shader::SourceKey(path);

		const auto found = std::find_if(Entries().begin(), Entries().end(), [&key](const Entry& entry) { return entry.path == key; });

		return found != Entries().end() ? found->source.c_str() : nullptr;
	};

	#pragma endregion
};

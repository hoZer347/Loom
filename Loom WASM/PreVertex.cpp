#include "PreVertex.h"

#include "Material.h"
#include "Renderer.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Paths

		// One spelling per shader, whether or not it was named with its extension.
		std::string ShaderKey(const std::string& path)
		{
			const std::string key = std::filesystem::path(path).lexically_normal().generic_string();

			return key.ends_with(Shader::extension) ? key : key + Shader::extension;
		};

		#pragma endregion
	};

	// Out of the class, where clang sees Cover's member initialisers.
	PreVertex::Cover PreVertex::s_cover{ };

	#pragma region Lifetime

	PreVertex::~PreVertex()
	{
		if (std::erase(s_attached, this))
			Refresh();
	};

	void PreVertex::OnAttach()
	{
		s_attached.push_back(this);
		Refresh();
	};

	void PreVertex::OnDetach()
	{
		if (std::erase(s_attached, this))
			Refresh();
	};

	void PreVertex::OnFieldChanged(const SerializedField& field)
	{
		if (std::ranges::find(s_attached, this) != s_attached.end())
			Refresh();
	};

	#pragma endregion

	#pragma region Editor

	bool PreVertex::IsShown(const SerializedField& field) const
	{
		return field.data != &*shaders || !everything;
	};

	#pragma endregion

	#pragma region Shaders

	std::string PreVertex::CodeFor(const std::string& shader)
	{
		std::scoped_lock lock{ s_mutex };

		return s_cover.Covers(ShaderKey(shader)) ? s_cover.code : std::string{ };
	};

	void PreVertex::ReloadIfChanged()
	{
		Refresh();
	};

	#pragma endregion

	#pragma region Cover

	bool PreVertex::Cover::Covers(const std::string& shader) const
	{
		return !code.empty() && (everything || std::ranges::find(shaders, shader) != shaders.end());
	};

	void PreVertex::Refresh()
	{
		Cover next{ };

		if (!s_attached.empty())
		{
			const PreVertex& last = *s_attached.back();

			next.file = std::filesystem::path((const std::string&)*last.file).lexically_normal().generic_string();
			next.everything = last.everything;

			for (const std::string& shader : *last.shaders)
				if (!shader.empty())
					next.shaders.push_back(ShaderKey(shader));
		};

		Cover previous{ };

		{
			std::scoped_lock lock{ s_mutex };

			previous = s_cover;
		};

		if (!next.file.empty())
		{
#ifdef __EMSCRIPTEN__
			if (next.file == previous.file)
				next.code = previous.code;
			else
				Shader::Request(next.file, next.code);
#else
			std::error_code code;
			next.written = std::filesystem::last_write_time(next.file, code);

			if (next.file == previous.file && next.written == previous.written)
				next.code = previous.code;
			else if (std::ifstream source{ next.file })
				next.code.assign(std::istreambuf_iterator<char>(source), { });
#endif
		};

		if (next == previous)
			return;

		{
			std::scoped_lock lock{ s_mutex };

			s_cover = next;
		};

		// None left to compile with once the engine is shutting down.
		if (!Renderer::Get())
			return;

		// Outside the lock, since compiling asks CodeFor.
		Material::ReloadShaders(
			[&](const std::string& shader)
			{
				const std::string key = ShaderKey(shader);

				return previous.Covers(key) || next.Covers(key);
			});
	};

	#pragma endregion
};

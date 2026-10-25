#include "ProjectAssets.h"

#include "ComponentRegistry.h"
#include "Prefab.h"
#include "SceneSerializer.h"
#include "SpriteAsset.h"

#include "Utilities/StateReference.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr size_t maxNameLength = 64;

		// Scripts, shaders, textures, sounds and fonts aside.
		constexpr std::string_view gameFileExtensions[] =
		{
			SceneSerializer::extension,
			Prefab::extension,
		};

		constexpr std::string_view shaderExtensions[] = { ProjectAssets::shaderExtension };

		// Whatever Textures.cpp has stb_image decode.
		constexpr std::string_view textureExtensions[] = { ProjectAssets::textureExtension, ".jpg", ".jpeg", ".bmp", ".tga" };

		// 2D art besides textures.
		constexpr std::string_view twoDExtensions[] = { SpriteAsset::extension, Palette::extension };

		// Whatever Audio.cpp has miniaudio decode.
		constexpr std::string_view soundExtensions[] = { ".wav", ".mp3", ".flac" };

		// What a UiText bakes its text from.
		constexpr std::string_view fontExtensions[] = { ".ttf", ".otf", ".ttc" };

		template <size_t count>
		bool HasExtension(const std::string& name, const std::string_view (&extensions)[count])
		{
			std::string extension = std::filesystem::path(name).extension().string();

			std::transform(
				extension.begin(),
				extension.end(),
				extension.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });

			return std::find(std::begin(extensions), std::end(extensions), extension) != std::end(extensions);
		};

		// Windows forbids the first lot in a file name; the rest mean something
		// to MSBuild, or to the XML a project is written in.
		constexpr const char* const forbiddenNameCharacters = "<>:\"/\\|?*&;%'$@()";

		constexpr const char* const keywords[] =
		{
			"alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char",
			"char8_t", "char16_t", "char32_t", "class", "concept", "const", "consteval",
			"constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
			"co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
			"else", "enum", "explicit", "export", "extern", "false", "float", "for",
			"friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
			"noexcept", "nullptr", "operator", "private", "protected", "public", "register",
			"reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
			"static_assert", "static_cast", "struct", "switch", "template", "this",
			"thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union",
			"unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
		};

		void Fail(std::string* error, const std::string& reason)
		{
			if (error)
				*error = reason;
		};

		bool WriteNew(const std::filesystem::path& path, const std::string& text, std::string* error)
		{
			std::error_code code;

			if (std::filesystem::exists(path, code))
			{
				Fail(error, path.string() + " already exists");
				return false;
			};

			std::ofstream out(path, std::ios::binary);

			if (!out)
			{
				Fail(error, "Could not write " + path.string());
				return false;
			};

			out << text;

			return true;
		};

		// The namespace the scripts project puts its code in, or the project's
		// own name when it does not say.
		std::string RootNamespace(const std::filesystem::path& project)
		{
			const std::string name = ProjectAssets::Element(ProjectAssets::ReadText(project.string()), "RootNamespace");

			return name.empty()
				? project.stem().string()
				: name;
		};

		// A whole script in one header, which the scripts project picks up by
		// its **\*.hpp glob and which registers itself as a component.
		const char* const script = R"(#pragma once

#include "Loom.h"


namespace {NAMESPACE}
{
	struct {NAME} : Loom::Component<{NAME}>
	{
		void OnAttach() override
		{
		};

		void OnUpdate() override
		{
		};
	};
};
)";

		const char* const state = R"(#pragma once

#include "Loom.h"


namespace {NAMESPACE}
{
	// One step of a state machine's behaviour. Deriving from Loom::State is
	// all it takes for a StateMachine's Start and Current to offer it.
	// gameObject is the object the machine is on. Proceed() moves the machine
	// on to the next queued state, SetState<Other>() straight to another.
	struct {NAME} : Loom::State<{NAME}>
	{
		void OnEnter(Loom::StateBase* lastState) override
		{
		};

		void OnUpdate() override
		{
		};

		void OnExit(Loom::StateBase* nextState) override
		{
		};
	};
};
)";

		const char* const state_machine = R"(#pragma once

#include "Loom.h"


namespace {NAMESPACE}
{
	// A component whose behaviour is its states. Deriving from
	// Loom::StateMachine is all it takes to appear under Add Component, with
	// Start and Current to pick its states from.
	struct {NAME} : Loom::StateMachine<{NAME}>
	{
		void OnStart() override
		{
		};

		void OnUpdate() override
		{
		};
	};
};
)";

		// A PNG with its pixel data stored rather than deflated, which is a
		// valid zlib stream and needs no compressor.
		namespace Png
		{
			constexpr uint32_t crcPolynomial = 0xEDB88320u;
			constexpr uint32_t adlerModulus = 65521u;
			constexpr size_t maxStoredBlock = 0xFFFF;
			constexpr uint8_t bitDepth = 8;
			constexpr uint8_t colourTypeRgba = 6;
			constexpr uint8_t zlibMethod = 0x78;
			constexpr uint8_t zlibNoCompressionCheck = 0x01;
			constexpr uint8_t filterNone = 0;
			constexpr size_t channels = 4;
			constexpr uint8_t signature[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

			uint32_t Crc(const std::vector<uint8_t>& bytes)
			{
				static const std::array<uint32_t, 256> table =
					[]()
					{
						std::array<uint32_t, 256> values{ };

						for (uint32_t n = 0; n < values.size(); n++)
						{
							uint32_t c = n;

							for (int bit = 0; bit < 8; bit++)
								c = (c & 1) ? crcPolynomial ^ (c >> 1) : c >> 1;

							values[n] = c;
						};

						return values;
					}();

				uint32_t c = 0xFFFFFFFFu;

				for (const uint8_t byte : bytes)
					c = table[(c ^ byte) & 0xFF] ^ (c >> 8);

				return c ^ 0xFFFFFFFFu;
			};

			void Put32(std::vector<uint8_t>& out, uint32_t value)
			{
				for (int shift = 24; shift >= 0; shift -= 8)
					out.push_back((uint8_t)(value >> shift));
			};

			void PutChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
			{
				Put32(out, (uint32_t)data.size());

				std::vector<uint8_t> typed(type, type + 4);
				typed.insert(typed.end(), data.begin(), data.end());

				out.insert(out.end(), typed.begin(), typed.end());
				Put32(out, Crc(typed));
			};

			std::vector<uint8_t> Encode(uint32_t width, uint32_t height, const std::array<uint8_t, channels>& rgba)
			{
				std::vector<uint8_t> raw;

				for (uint32_t y = 0; y < height; y++)
				{
					raw.push_back(filterNone);

					for (uint32_t x = 0; x < width; x++)
						raw.insert(raw.end(), rgba.begin(), rgba.end());
				};

				std::vector<uint8_t> zlib = { zlibMethod, zlibNoCompressionCheck };

				for (size_t at = 0; at < raw.size(); at += maxStoredBlock)
				{
					const uint16_t length = (uint16_t)std::min(maxStoredBlock, raw.size() - at);
					const bool last = at + length == raw.size();

					zlib.push_back(last ? 1 : 0);
					zlib.push_back((uint8_t)length);
					zlib.push_back((uint8_t)(length >> 8));
					zlib.push_back((uint8_t)~length);
					zlib.push_back((uint8_t)(~length >> 8));
					zlib.insert(zlib.end(), raw.begin() + (ptrdiff_t)at, raw.begin() + (ptrdiff_t)at + length);
				};

				uint32_t a = 1, b = 0;

				for (const uint8_t byte : raw)
				{
					a = (a + byte) % adlerModulus;
					b = (b + a) % adlerModulus;
				};

				Put32(zlib, (b << 16) | a);

				std::vector<uint8_t> header;
				Put32(header, width);
				Put32(header, height);
				header.insert(header.end(), { bitDepth, colourTypeRgba, 0, 0, 0 });

				std::vector<uint8_t> png(std::begin(signature), std::end(signature));

				PutChunk(png, "IHDR", header);
				PutChunk(png, "IDAT", zlib);
				PutChunk(png, "IEND", { });

				return png;
			};
		};
	};

	const char* const ProjectAssets::defaultShader = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform vec3 u_color;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0);
}
)";

	std::string ProjectAssets::Replace(std::string text, const std::string& token, const std::string& value)
	{
		// Resuming past the replacement, not at it: a value that contains its
		// own token would otherwise be substituted forever.
		for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
			text.replace(at, token.size(), value);

		return text;
	};

	std::string ProjectAssets::ReadText(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);

		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};

	std::string ProjectAssets::Element(const std::string& xml, const std::string& tag)
	{
		const std::string open = '<' + tag + '>';
		const size_t begin = xml.find(open);

		if (begin == std::string::npos)
			return "";

		const size_t end = xml.find("</" + tag + '>', begin);

		return end == std::string::npos
			? ""
			: xml.substr(begin + open.size(), end - begin - open.size());
	};

	bool ProjectAssets::IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > maxNameLength)
			return false;

		if (name.back() == '.' || name.back() == ' ' || name.front() == ' ')
			return false;

		for (const char c : name)
			if ((unsigned char)c < ' ' || strchr(forbiddenNameCharacters, c) != nullptr)
				return false;

		return true;
	};

	std::string ProjectAssets::ScriptNameProblem(const std::string& scripts_project, const std::string& name)
	{
		const auto identifier =
			[&name]()
			{
				if (name.empty() || name.size() > maxNameLength || isdigit((unsigned char)name.front()))
					return false;

				for (const char c : name)
					if (!isalnum((unsigned char)c) && c != '_')
						return false;

				return true;
			};

		if (!identifier())
			return "Letters, digits and _; it becomes a C++ type.";

		for (const char* keyword : keywords)
			if (name == keyword)
				return "'" + name + "' is a C++ keyword.";

		// Components register by type name without their namespace, and the
		// registry turns away a second one by a name it already has.
		if (ComponentRegistry::All().contains(name))
			return "A component called " + name + " already exists.";

		if (StateRegistry::Contains(name))
			return "A state called " + name + " already exists.";

		const std::filesystem::path root = std::filesystem::path(scripts_project).parent_path();
		const std::string header = name + ".hpp";

		// A header can hold several components, and the registry only knows the
		// ones in a library that is built and loaded, so the headers are read too.
		std::error_code code;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, code))
		{
			if (entry.path().filename() == header)
				return "There is already a " + header + " in " + entry.path().parent_path().string() + '.';

			if (entry.path().extension() != ".hpp")
				continue;

			const std::vector<std::string> declared = DeclaredTypes(ReadText(entry.path().string()));

			if (std::find(declared.begin(), declared.end(), name) != declared.end())
				return "A type called " + name + " is already in " + entry.path().filename().string() + '.';
		};

		return "";
	};

	std::vector<std::string> ProjectAssets::DeclaredTypes(const std::string& text)
	{
		static const std::regex declared("\\b(?:struct|class)\\s+(\\w+)\\s*(?:final\\s*)?[:{]");

		std::vector<std::string> names;

		for (auto match = std::sregex_iterator(text.begin(), text.end(), declared); match != std::sregex_iterator(); match++)
			names.push_back((*match)[1].str());

		return names;
	};

	int ProjectAssets::DeclarationLine(const std::string& text, const std::string& name)
	{
		// Anything but a plain name would be read as part of the pattern.
		if (name.empty() ||
			!std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }) ||
			text.find(name) == std::string::npos)
			return 0;

		static const std::regex template_head("\\btemplate\\s*<");
		const std::regex declared("\\b(?:struct|class)\\s+(?:\\w+\\s+)?" + name + "\\s*(?:final\\s*)?(?::(?!:)|\\{)");

		for (auto match = std::sregex_iterator(text.begin(), text.end(), declared); match != std::sregex_iterator(); match++)
		{
			const size_t start = (size_t)match->position();
			const size_t newline = text.rfind('\n', start);
			const size_t line_start = newline == std::string::npos ? 0 : newline + 1;

			if (text.substr(line_start, start - line_start).find("//") != std::string::npos)
				continue;

			const size_t opened = text.rfind("/*", start);
			const size_t closed = text.rfind("*/", start);

			if (opened != std::string::npos && (closed == std::string::npos || closed < opened))
				continue;

			// A component is never a primary template, only what one is derived
			// from, so a template head on this line or the one above rules it out.
			const size_t previous = newline == std::string::npos || newline == 0
				? line_start
				: text.rfind('\n', newline - 1) + 1;

			if (std::regex_search(text.substr(previous, start - previous), template_head))
				continue;

			return 1 + (int)std::count(text.begin(), text.begin() + start, '\n');
		};

		return 0;
	};

	std::vector<ProjectAssets::DeclaredScript> ProjectAssets::DeclaredScripts(const std::string& text)
	{
		static const std::regex declared("\\b(?:struct|class)\\s+(\\w+)\\s*(?:final\\s*)?:([^{;]*)\\{");
		static const std::regex state("\\bState\\s*<");

		std::vector<DeclaredScript> scripts;

		for (auto match = std::sregex_iterator(text.begin(), text.end(), declared); match != std::sregex_iterator(); match++)
		{
			const std::string name = (*match)[1].str();
			const std::string bases = (*match)[2].str();
			const std::regex itself("<\\s*" + name + "\\s*[,>]");

			if (std::regex_search(bases, itself))
				scripts.push_back(DeclaredScript{ name, std::regex_search(bases, state) });
		};

		return scripts;
	};

	std::string ProjectAssets::CreateFolder(const std::string& folder, const std::string& name, std::string* error)
	{
		const std::filesystem::path path = std::filesystem::path(folder) / name;

		std::error_code code;

		if (!IsValidName(name) || !std::filesystem::create_directory(path, code))
		{
			Fail(error, "Could not create the folder " + path.string());
			return "";
		};

		return path.string();
	};

	std::string ProjectAssets::CreateShader(const std::string& folder, const std::string& name, std::string* error)
	{
		const std::filesystem::path path = std::filesystem::path(folder) / (name + shaderExtension);

		if (!IsValidName(name) || !WriteNew(path, defaultShader, error))
			return "";

		return path.string();
	};

	std::string ProjectAssets::CreateTexture(const std::string& folder, const std::string& name, std::string* error)
	{
		constexpr uint32_t size = 64;
		constexpr std::array<uint8_t, Png::channels> white = { 0xFF, 0xFF, 0xFF, 0xFF };

		const std::filesystem::path path = std::filesystem::path(folder) / (name + textureExtension);

		const std::vector<uint8_t> png = Png::Encode(size, size, white);

		if (!IsValidName(name) || !WriteNew(path, std::string(png.begin(), png.end()), error))
			return "";

		return path.string();
	};

	std::string ProjectAssets::CreatePalette(const std::string& folder, const std::string& name, std::string* error)
	{
		const std::filesystem::path path = std::filesystem::path(folder) / (name + Palette::extension);

		if (!IsValidName(name) || !WriteNew(path, "", error))
			return "";

		return path.string();
	};

	std::string ProjectAssets::CopyInto(const std::string& file, const std::string& folder, std::string* error)
	{
		const std::filesystem::path source(file);
		std::error_code code;

		if (!std::filesystem::is_regular_file(source, code))
		{
			Fail(error, file + " is not a file");
			return "";
		};

		if (std::filesystem::equivalent(source.parent_path(), folder, code))
			return source.string();

		std::filesystem::path target = std::filesystem::path(folder) / source.filename();

		for (int suffix = 1; std::filesystem::exists(target, code); suffix++)
			target = std::filesystem::path(folder) / (source.stem().string() + std::to_string(suffix) + source.extension().string());

		if (!std::filesystem::copy_file(source, target, code))
		{
			Fail(error, "Could not copy " + file + " to " + target.string() + ": " + code.message());
			return "";
		};

		return target.string();
	};

	std::string ProjectAssets::MoveInto(const std::string& path, const std::string& folder, std::string* error)
	{
		const std::filesystem::path source(path);
		std::error_code code;

		if (!std::filesystem::exists(source, code))
		{
			Fail(error, path + " does not exist");
			return "";
		};

		if (std::filesystem::equivalent(source.parent_path(), folder, code))
			return source.string();

		if (IsUnder(folder, path))
		{
			Fail(error, "Could not move " + path + " inside itself");
			return "";
		};

		const std::filesystem::path target = std::filesystem::path(folder) / source.filename();

		if (std::filesystem::exists(target, code))
		{
			Fail(error, "Could not move " + path + ": " + target.string() + " already exists");
			return "";
		};

		std::filesystem::rename(source, target, code);

		if (code)
		{
			Fail(error, "Could not move " + path + " to " + target.string() + ": " + code.message());
			return "";
		};

		return target.string();
	};

	std::string ProjectAssets::Relocate(const std::string& path, const std::string& from, const std::string& to)
	{
		std::error_code code;
		const std::filesystem::path absolute = std::filesystem::absolute(path, code).lexically_normal();

		if (!IsUnder(absolute.string(), from))
			return path;

		const std::filesystem::path relative = absolute.lexically_relative(from);

		return relative == "."
			? to
			: (std::filesystem::path(to) / relative).string();
	};

	std::string ProjectAssets::CreateScript(
		const std::string& scripts_project,
		const std::string& folder,
		const std::string& name,
		ScriptKind kind,
		std::string* error)
	{
		if (const std::string problem = ScriptNameProblem(scripts_project, name); !problem.empty())
		{
			Fail(error, problem);
			return "";
		};

		const std::filesystem::path root = std::filesystem::path(scripts_project).parent_path();
		const std::filesystem::path header = std::filesystem::path(folder) / (name + ".hpp");

		const std::filesystem::path relative = std::filesystem::path(folder).lexically_relative(root);

		if (relative.empty() || *relative.begin() == "..")
		{
			Fail(error, "Scripts go under " + root.string());
			return "";
		};

		// A folder made outside the editor never went through IsValidName, and
		// the project's glob hands its name to MSBuild as written.
		for (const std::filesystem::path& part : relative)
			if (part != "." && !IsValidName(part.string()))
			{
				Fail(error, "'" + part.string() + "' cannot hold scripts: folder names reach MSBuild, which cannot take " + forbiddenNameCharacters);
				return "";
			};

		const char* const templates[] = { script, state, state_machine };

		const std::string text = Replace(
			Replace(templates[(int)kind], "{NAME}", name),
			"{NAMESPACE}",
			RootNamespace(scripts_project));

		return WriteNew(header, text, error)
			? header.string()
			: "";
	};

	ProjectAssets::Opener ProjectAssets::OpenerOf(const std::string& path, const std::string& scripts_folder)
	{
		const std::filesystem::path extension = std::filesystem::path(path).extension();

		if (extension == SceneSerializer::extension)
			return Opener::Editor;

		if (extension == Prefab::extension)
			return Opener::Prefab;

		if (extension == shaderExtension)
			return Opener::Shaders;

		if (IsTextureFile(path) || extension == SpriteAsset::extension)
			return Opener::Sprites;

		if (extension == Palette::extension)
			return Opener::Palette;

		return IsScriptFile(path) && !scripts_folder.empty() && IsUnder(path, scripts_folder)
			? Opener::VisualStudio
			: Opener::Shell;
	};

	bool ProjectAssets::IsUnder(const std::string& path, const std::string& folder)
	{
		const std::filesystem::path relative =
			std::filesystem::path(path).lexically_relative(folder);

		return !relative.empty() && *relative.begin() != "..";
	};

	std::string ProjectAssets::RelativeTo(const std::string& path, const std::string& folder)
	{
		return std::filesystem::path(path).lexically_normal().lexically_relative(folder).generic_string();
	};

	bool ProjectAssets::IsGameFile(const std::string& name)
	{
		return HasExtension(name, gameFileExtensions) || IsShaderFile(name) || IsTwoDFile(name) || IsSoundFile(name) || IsFontFile(name);
	};

	bool ProjectAssets::IsScriptFile(const std::string& name)
	{
		return HasExtension(name, scriptExtensions);
	};

	bool ProjectAssets::IsShaderFile(const std::string& name)
	{
		return HasExtension(name, shaderExtensions);
	};

	bool ProjectAssets::IsTextureFile(const std::string& name)
	{
		return HasExtension(name, textureExtensions);
	};

	bool ProjectAssets::IsTwoDFile(const std::string& name)
	{
		return IsTextureFile(name) || HasExtension(name, twoDExtensions);
	};

	bool ProjectAssets::IsSoundFile(const std::string& name)
	{
		return HasExtension(name, soundExtensions);
	};

	bool ProjectAssets::IsFontFile(const std::string& name)
	{
		return HasExtension(name, fontExtensions);
	};
};

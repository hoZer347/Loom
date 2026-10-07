#include "ProjectAssets.h"

#include "ComponentRegistry.h"

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

#include "Component.h"
#include "GameObject.h"


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

#include "Utilities/StateMachine.h"


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

#include "Utilities/StateMachine.h"


namespace {NAMESPACE}
{
	// A component whose behaviour is its states. Deriving from
	// Loom::StateMachine is all it takes to appear under Add Component, with
	// Start and Current to pick its states from.
	struct {NAME} : Loom::StateMachine<{NAME}>
	{
	protected:
		void OnMachineStart() override
		{
		};

		void OnMachineUpdate() override
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

void main()
{
    gl_Position = u_model * vec4(aPos, 1.0);
}


// ===FRAGMENT===

out vec4 FragColor;

void main()
{
    FragColor = vec4(0.35, 0.8, 0.45, 1.0);
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
		const std::regex declared("\\b(struct|class)\\s+" + name + "\\s*(final\\s*)?[:{]");

		std::error_code code;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, code))
		{
			if (entry.path().filename() == header)
				return "There is already a " + header + " in " + entry.path().parent_path().string() + '.';

			if (entry.path().extension() != ".hpp")
				continue;

			std::ifstream in(entry.path(), std::ios::binary);
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

			if (std::regex_search(text, declared))
				return "A type called " + name + " is already in " + entry.path().filename().string() + '.';
		};

		return "";
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

	bool ProjectAssets::IsUnder(const std::string& path, const std::string& folder)
	{
		const std::filesystem::path relative =
			std::filesystem::path(path).lexically_relative(folder);

		return !relative.empty() && *relative.begin() != "..";
	};
};

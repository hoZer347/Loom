#include "ProjectAssets.h"

#include "ProjectTemplate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr size_t maxNameLength = 64;

		constexpr const char* const registerEntryPoint = "LoomRegisterScripts";
		constexpr const char* const registerCall = "ComponentRegistry::Register<";

		// Windows forbids the first lot in a file name; the rest mean something
		// to MSBuild, or to the XML a project is written in.
		constexpr const char* const forbiddenNameCharacters = "<>:\"/\\|?*&;%'$@()";

		// Where a script's header could clash with the engine's: a quoted include
		// looks beside the including file first, then in these.
		constexpr const char* const engineIncludeFolders[] = { "Loom WASM", "Loom Math", "Loom ImGui" };

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

		// A text file as lines, remembering its line ending so an edit writes it
		// back the way Visual Studio left it.
		struct TextFile
		{
			std::filesystem::path path;
			std::vector<std::string> lines;
			bool crlf = false;

			bool Read(const std::filesystem::path& from)
			{
				path = from;

				std::ifstream in(path, std::ios::binary);

				if (!in)
					return false;

				std::stringstream buffer;
				buffer << in.rdbuf();

				const std::string text = buffer.str();
				crlf = text.find("\r\n") != std::string::npos;

				std::string line;
				std::istringstream stream(text);

				while (std::getline(stream, line))
				{
					if (!line.empty() && line.back() == '\r')
						line.pop_back();

					lines.push_back(line);
				};

				return true;
			};

			bool Write() const
			{
				std::ofstream out(path, std::ios::binary);

				if (!out)
					return false;

				for (const std::string& line : lines)
					out << line << (crlf ? "\r\n" : "\n");

				return true;
			};

			size_t FindLast(const std::function<bool(const std::string&)>& matches) const
			{
				for (size_t at = lines.size(); at-- > 0;)
					if (matches(lines[at]))
						return at;

				return std::string::npos;
			};

			size_t FindLast(const std::string& text) const
			{
				return FindLast([&text](const std::string& line) { return line.find(text) != std::string::npos; });
			};
		};

		std::string IndentOf(const std::string& line)
		{
			return line.substr(0, line.find_first_not_of(" \t"));
		};

		// Puts an item beside the last one of its kind in an MSBuild file, or
		// in an item group of its own when there is none to sit beside.
		void AddItem(TextFile& file, const std::string& element, const std::string& include)
		{
			const std::string opening = '<' + element + " Include=";

			const size_t found = file.FindLast(opening);

			if (found != std::string::npos)
			{
				size_t at = found;

				// A filters file spells an item over several lines.
				if (file.lines[at].find("/>") == std::string::npos)
					while (at + 1 < file.lines.size() && file.lines[at].find("</" + element + '>') == std::string::npos)
						at++;

				file.lines.insert(
					file.lines.begin() + (ptrdiff_t)at + 1,
					IndentOf(file.lines[found]) + opening + '"' + include + "\" />");

				return;
			};

			size_t anchor = file.FindLast("Microsoft.Cpp.targets");

			if (anchor == std::string::npos)
				anchor = file.FindLast("</Project>");

			if (anchor == std::string::npos)
				anchor = file.lines.size();

			file.lines.insert(
				file.lines.begin() + (ptrdiff_t)anchor,
				{ "  <ItemGroup>", "    " + opening + '"' + include + "\" />", "  </ItemGroup>" });
		};

		std::string ValueBetween(const TextFile& file, const std::string& open, const std::string& close)
		{
			const size_t at = file.FindLast(open);

			if (at == std::string::npos)
				return "";

			const std::string& line = file.lines[at];
			const size_t begin = line.find(open) + open.size();
			const size_t end = line.find(close, begin);

			return end == std::string::npos ? "" : line.substr(begin, end - begin);
		};

		// The .cpp that holds LoomRegisterScripts, wherever under the scripts
		// project it ended up.
		bool FindModule(const std::filesystem::path& root, TextFile& module)
		{
			std::error_code code;

			for (const auto& entry : std::filesystem::recursive_directory_iterator(root, code))
			{
				if (!entry.is_regular_file(code) || entry.path().extension() != ".cpp")
					continue;

				TextFile candidate;

				if (candidate.Read(entry.path()) && candidate.FindLast(registerEntryPoint) != std::string::npos)
				{
					module = std::move(candidate);
					return true;
				};
			};

			return false;
		};

		bool Register(TextFile& module, const std::string& include, const std::string& type, const std::string& name)
		{
			size_t call = module.FindLast(registerCall);
			std::string indent;

			if (call != std::string::npos)
				indent = IndentOf(module.lines[call]);
			else
			{
				// No script registered yet: the first line inside the function.
				const size_t entry = module.FindLast(registerEntryPoint);

				for (call = entry; call < module.lines.size(); call++)
					if (module.lines[call].find('{') != std::string::npos)
						break;

				if (call == module.lines.size())
					return false;

				indent = IndentOf(module.lines[call]) + '\t';
			};

			module.lines.insert(
				module.lines.begin() + (ptrdiff_t)call + 1,
				indent + "Loom::" + registerCall + type + ">(\"" + name + "\");");

			const size_t last_include = module.FindLast(
				[](const std::string& line) { return line.rfind("#include", 0) == 0; });

			module.lines.insert(
				module.lines.begin() + (ptrdiff_t)(last_include == std::string::npos ? 0 : last_include + 1),
				"#include \"" + include + '"');

			return true;
		};

		const char* const script_header = R"(#pragma once

#include "Component.h"


namespace {NAMESPACE}
{
	struct {NAME} : Loom::Component<{NAME}>
	{
		void OnAttach() override;
		void OnUpdate() override;
	};
};
)";

		const char* const script_source = R"(#include "{NAME}.h"

#include "GameObject.h"


namespace {NAMESPACE}
{
	void {NAME}::OnAttach()
	{
	};

	void {NAME}::OnUpdate()
	{
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

void main()
{
    gl_Position = vec4(aPos, 1.0);
};


// ===FRAGMENT===

out vec4 FragColor;

void main()
{
    FragColor = vec4(0.35, 0.8, 0.45, 1.0);
};
)";

	std::string ProjectAssets::Replace(std::string text, const std::string& token, const std::string& value)
	{
		// Resuming past the replacement, not at it: a value that contains its
		// own token would otherwise be substituted forever.
		for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
			text.replace(at, token.size(), value);

		return text;
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

		const std::filesystem::path root = std::filesystem::path(scripts_project).parent_path();
		const std::string header = name + ".h";

		std::error_code code;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, code))
			if (entry.path().filename() == header)
				return "There is already a " + header + " in " + entry.path().parent_path().string() + '.';

		const std::filesystem::path engine = ProjectTemplate::EngineRoot();

		for (const char* folder : engineIncludeFolders)
			if (std::filesystem::exists(engine / folder / header, code))
				return header + " is an engine header; a script by that name would hide it.";

		TextFile module;

		if (FindModule(root, module) && module.FindLast("::" + name + '>') != std::string::npos)
			return "A script called " + name + " is already registered.";

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
		std::string* error)
	{
		if (const std::string problem = ScriptNameProblem(scripts_project, name); !problem.empty())
		{
			Fail(error, problem);
			return "";
		};

		const std::filesystem::path root = std::filesystem::path(scripts_project).parent_path();
		const std::filesystem::path header = std::filesystem::path(folder) / (name + ".h");
		const std::filesystem::path source = std::filesystem::path(folder) / (name + ".cpp");

		const std::filesystem::path relative = std::filesystem::path(folder).lexically_relative(root);

		if (relative.empty() || *relative.begin() == "..")
		{
			Fail(error, "Scripts go under " + root.string());
			return "";
		};

		// A folder made outside the editor never went through IsValidName, and
		// its name goes into the project as written.
		for (const std::filesystem::path& part : relative)
			if (part != "." && !IsValidName(part.string()))
			{
				Fail(error, "'" + part.string() + "' cannot hold scripts: folder names go into the project, which cannot take " + forbiddenNameCharacters);
				return "";
			};

		// Everything is read and edited before anything is written, so a
		// project that cannot take the script is left as it was.
		TextFile project, filters, module;

		if (!project.Read(scripts_project))
		{
			Fail(error, "Could not read " + scripts_project);
			return "";
		};

		if (!FindModule(root, module))
		{
			Fail(error, std::string("No .cpp under ") + root.string() + " defines " + registerEntryPoint);
			return "";
		};

		std::string space = ValueBetween(project, "<RootNamespace>", "</RootNamespace>");

		if (space.empty())
			space = std::filesystem::path(scripts_project).stem().string();

		const std::string project_header = header.lexically_relative(root).make_preferred().string();
		const std::string project_source = source.lexically_relative(root).make_preferred().string();
		const std::string include = header.lexically_relative(module.path.parent_path()).generic_string();

		if (!Register(module, include, space + "::" + name, name))
		{
			Fail(error, "Could not find where " + module.path.string() + " registers its scripts");
			return "";
		};

		AddItem(project, "ClCompile", project_source);
		AddItem(project, "ClInclude", project_header);

		const bool has_filters = filters.Read(scripts_project + ".filters");

		if (has_filters)
		{
			AddItem(filters, "ClCompile", project_source);
			AddItem(filters, "ClInclude", project_header);
		};

		const auto fill =
			[&](const char* text)
			{
				return Replace(Replace(text, "{NAME}", name), "{NAMESPACE}", space);
			};

		if (!WriteNew(header, fill(script_header), error))
			return "";

		if (!WriteNew(source, fill(script_source), error))
		{
			std::error_code code;
			std::filesystem::remove(header, code);

			return "";
		};

		if (!project.Write() || !module.Write() || (has_filters && !filters.Write()))
		{
			Fail(error, "Wrote " + name + " but could not add it to " + scripts_project);
			return "";
		};

		return source.string();
	};
};

#include "ShaderVariables.h"

#include "EditorGui.h"
#include "Renderer.h"
#include "Textures.h"

#include "imgui.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		// Set by Material, Mesh, Camera and Light, so never a variable.
		constexpr std::string_view ENGINE_UNIFORMS[] =
		{
			"u_model",
			"u_color",
			"u_shadowsOff",
			"u_viewProjection",
			"u_cameraPosition",
			"u_lightViewProjection",
			"u_lightDirection",
			"u_lightColor",
			"u_ambient",
			"u_shadowMap",
		};

		constexpr const char* INSTANCE_TAG = "instance";
		constexpr const char* MATERIAL_TAG = "material";

		// GLSL keeps gl_ for itself.
		constexpr std::string_view RESERVED_PREFIX = "gl_";

		constexpr float FLOAT_DRAG_SPEED = 0.01f;
		constexpr float INTEGER_DRAG_SPEED = 0.2f;
		constexpr const char* FLOAT_FORMAT = "%.3f";
		constexpr const char* INTEGER_FORMAT = "%.0f";

		// Enough digits for a float, the precision every float variable has in
		// the end, to come back the same.
		constexpr const char* WRITTEN_FLOAT_FORMAT = "%.9g";
		constexpr const char* WRITTEN_INTEGER_FORMAT = "%.0f";

		constexpr size_t NUMBER_LENGTH = 64;

		const std::regex& DeclarationPattern()
		{
			// The indentation, the type, the name and the word in a trailing
			// comment.
			static const std::regex pattern(
				R"(^(\s*)uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)\s*;\s*(?://\s*(\w+))?.*$)");

			return pattern;
		};

		constexpr size_t TYPE_MATCH = 2;
		constexpr size_t NAME_MATCH = 3;
		constexpr size_t TAG_MATCH = 4;

		bool IsEngineUniform(std::string_view name)
		{
			return std::find(std::begin(ENGINE_UNIFORMS), std::end(ENGINE_UNIFORMS), name) != std::end(ENGINE_UNIFORMS);
		};

		std::string Trimmed(const std::string& line)
		{
			const size_t start = line.find_first_not_of(" \t");

			return start == std::string::npos ? "" : line.substr(start);
		};

		bool IsSectionMarker(const std::string& line)
		{
			return
				line.find(COMMON_SECTION) != std::string::npos ||
				line.find(VERTEX_SECTION) != std::string::npos ||
				line.find(FRAGMENT_SECTION) != std::string::npos;
		};

		// A shader file as lines, and the line ending to write it back with.
		struct ShaderFile
		{
			std::vector<std::string> lines;
			std::string newline = "\n";

			bool Read(const std::string& path, std::string& error)
			{
				std::ifstream file(path, std::ios::binary);

				if (!file)
				{
					error = "Could not open " + path;
					return false;
				};

				std::string line;

				while (std::getline(file, line))
				{
					if (!line.empty() && line.back() == '\r')
					{
						line.pop_back();
						newline = "\r\n";
					};

					lines.push_back(line);
				};

				return true;
			};

			bool Write(const std::string& path, std::string& error) const
			{
				std::ofstream file(path, std::ios::binary | std::ios::trunc);

				for (const std::string& line : lines)
					file << line << newline;

				if (!file)
					error = "Could not write " + path;

				return (bool)file;
			};

			// Every line, since each stage can declare the same uniform.
			std::vector<size_t> Declaring(const std::string& name) const
			{
				std::vector<size_t> found;
				std::smatch match;

				for (size_t i = 0; i < lines.size(); i++)
					if (std::regex_match(lines[i], match, DeclarationPattern()) && match[NAME_MATCH] == name)
						found.push_back(i);

				return found;
			};

			// After the declarations and precision statements already at the top
			// of COMMON, adding the section in front of the first stage when
			// there is none.
			size_t InsertionPoint()
			{
				const auto common = std::find_if(
					lines.begin(),
					lines.end(),
					[](const std::string& line) { return line.find(COMMON_SECTION) != std::string::npos; });

				if (common == lines.end())
				{
					const size_t first = std::find_if(lines.begin(), lines.end(), IsSectionMarker) - lines.begin();

					lines.insert(lines.begin() + first, { std::string("// ") + COMMON_SECTION, "", "" });

					return first + 2;
				};

				size_t at = common - lines.begin() + 1;

				for (size_t i = at; i < lines.size() && !IsSectionMarker(lines[i]); i++)
				{
					const std::string trimmed = Trimmed(lines[i]);

					if (trimmed.starts_with("uniform") || trimmed.starts_with("precision"))
						at = i + 1;
				};

				return at;
			};
		};

		std::string Declaration(const ShaderVariable& variable)
		{
			return
				std::string("uniform ") + InfoOf(variable.type).glsl + ' ' + variable.name + "; // " +
				(variable.scope == UniformScope::Instance ? INSTANCE_TAG : MATERIAL_TAG);
		};

		bool IsIdentifier(const std::string& name)
		{
			static const std::regex identifier(R"([A-Za-z_]\w*)");

			return std::regex_match(name, identifier);
		};

		const UniformValue& DefaultOf(UniformType type)
		{
			static const std::vector<UniformValue> defaults = []()
				{
					std::vector<UniformValue> all;

					for (const UniformTypeInfo& info : UNIFORM_TYPES)
						all.push_back(UniformValue::Default(info.type));

					return all;
				}();

			return defaults[(int)type];
		};

		// White, for a sampler with no image: something has to be behind it.
		uint32_t BlankTexture(Renderer& renderer)
		{
			static const uint32_t blank = [&renderer]() -> uint32_t
				{
					constexpr uint8_t WHITE[] = { 255, 255, 255, 255 };
					return renderer.CreateTexture(1, 1, WHITE);
				}();

			return blank;
		};

		// An image that will not load is tried again once its file changes,
		// rather than every frame, which would say why every frame. A path with
		// no file behind it, one still being typed among them, says nothing.
		uint32_t TextureFor(Renderer& renderer, const UniformValue* value)
		{
			if (value == nullptr || value->texture.empty())
				return BlankTexture(renderer);

			static std::unordered_map<std::string, std::filesystem::file_time_type> failed;

			std::error_code code;
			const std::filesystem::file_time_type written = std::filesystem::last_write_time(value->texture, code);

			if (code)
				return BlankTexture(renderer);

			const auto tried = failed.find(value->texture);

			if (tried != failed.end() && tried->second == written)
				return BlankTexture(renderer);

			const Texture* texture = Texture::Shared(value->texture);

			if (texture && texture->handle)
				return texture->handle;

			failed[value->texture] = written;

			return BlankTexture(renderer);
		};

		bool DrawNumbers(const UniformTypeInfo& info, UniformValue& value)
		{
			constexpr double INT_MIN_VALUE = std::numeric_limits<int32_t>::min();
			constexpr double INT_MAX_VALUE = std::numeric_limits<int32_t>::max();
			constexpr double UINT_MIN_VALUE = 0.0;
			constexpr double UINT_MAX_VALUE = std::numeric_limits<uint32_t>::max();

			const bool integer = info.kind != UniformKind::Float;
			const float speed = integer ? INTEGER_DRAG_SPEED : FLOAT_DRAG_SPEED;
			const char* format = integer ? INTEGER_FORMAT : FLOAT_FORMAT;
			const double* min = info.kind == UniformKind::Int ? &INT_MIN_VALUE : info.kind == UniformKind::UInt ? &UINT_MIN_VALUE : nullptr;
			const double* max = info.kind == UniformKind::Int ? &INT_MAX_VALUE : info.kind == UniformKind::UInt ? &UINT_MAX_VALUE : nullptr;

			bool changed = false;

			// A matrix reads the way it is written down, a row at a time, which
			// is across its columns.
			const float x = ImGui::GetCursorPosX();

			for (int row = 0; row < (IsMatrix(info) ? info.rows : 1); row++)
			{
				std::array<double, MAX_UNIFORM_COMPONENTS> shown{ };
				const int count = IsMatrix(info) ? info.columns : info.rows;

				for (int i = 0; i < count; i++)
					shown[i] = IsMatrix(info) ? value.components[i * info.rows + row] : value.components[i];

				ImGui::PushID(row);
				ImGui::SetCursorPosX(x);
				ImGui::SetNextItemWidth(-FLT_MIN);

				if (ImGui::DragScalarN("##value", ImGuiDataType_Double, shown.data(), count, speed, min, max, format))
				{
					changed = true;

					for (int i = 0; i < count; i++)
					{
						const double component = integer ? std::round(shown[i]) : shown[i];

						(IsMatrix(info) ? value.components[i * info.rows + row] : value.components[i]) = component;
					};
				};

				ImGui::PopID();
			};

			return changed;
		};

		bool DrawBools(const UniformTypeInfo& info, UniformValue& value)
		{
			bool changed = false;

			for (int i = 0; i < info.rows; i++)
			{
				bool on = value.components[i] != 0.0;

				if (i > 0)
					ImGui::SameLine();

				ImGui::PushID(i);

				if (ImGui::Checkbox("##value", &on))
				{
					value.components[i] = on ? 1.0 : 0.0;
					changed = true;
				};

				ImGui::PopID();
			};

			return changed;
		};

		bool DrawTexture(UniformValue& value)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);

			bool changed = InputTextString("##value", value.texture);

			changed |= AcceptAssetDrop(value.texture);

			ImGui::SetItemTooltip("An image in the project. Drag one in from the Project panel.");

			return changed;
		};
	};

	UniformValue UniformValue::Default(UniformType type)
	{
		UniformValue value;
		value.type = type;

		const UniformTypeInfo& info = InfoOf(type);

		if (IsMatrix(info))
			for (int i = 0; i < std::min(info.columns, info.rows); i++)
				value.components[i * info.rows + i] = 1.0;

		return value;
	};

	const UniformValue* UniformValues::Find(const ShaderVariable& variable) const
	{
		const auto found = values.find(variable.name);

		return found != values.end() && found->second.type == variable.type
			? &found->second
			: nullptr;
	};

	void UniformValues::Set(const std::string& name, UniformType type, std::initializer_list<double> components)
	{
		UniformValue value = UniformValue::Default(type);

		std::copy_n(components.begin(), std::min(components.size(), value.components.size()), value.components.begin());

		values[name] = value;
	};

	void UniformValues::SetTexture(const std::string& name, const std::string& path)
	{
		UniformValue value = UniformValue::Default(UniformType::Sampler2D);
		value.texture = path;

		values[name] = value;
	};

	std::string UniformValues::Write() const
	{
		std::string text;

		for (const auto& [name, value] : values)
		{
			const UniformTypeInfo& info = InfoOf(value.type);

			text += name + ' ' + info.glsl;

			if (info.kind == UniformKind::Texture)
				text += ' ' + value.texture;
			else
				for (int i = 0; i < info.columns * info.rows; i++)
				{
					char number[NUMBER_LENGTH]{ };

					if (info.kind == UniformKind::Float)
						snprintf(number, sizeof(number), WRITTEN_FLOAT_FORMAT, (float)value.components[i]);
					else snprintf(number, sizeof(number), WRITTEN_INTEGER_FORMAT, value.components[i]);

					text += ' ';
					text += number;
				};

			text += '\n';
		};

		return text;
	};

	void UniformValues::Read(const std::string& text)
	{
		values.clear();

		std::istringstream lines(text);
		std::string line;

		while (std::getline(lines, line))
		{
			std::istringstream words(line);
			std::string name;
			std::string glsl;
			UniformType type;

			if (!(words >> name >> glsl) || !FindUniformType(glsl, type))
				continue;

			UniformValue value = UniformValue::Default(type);
			const UniformTypeInfo& info = InfoOf(type);

			if (info.kind == UniformKind::Texture)
				std::getline(words >> std::ws, value.texture);
			else
				for (int i = 0; i < info.columns * info.rows; i++)
					words >> value.components[i];

			values[name] = value;
		};
	};

	std::vector<ShaderVariable> ParseShaderVariables(const std::string& source)
	{
		std::vector<ShaderVariable> variables;

		std::istringstream lines(source);
		std::string line;
		std::smatch match;

		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (!std::regex_match(line, match, DeclarationPattern()))
				continue;

			ShaderVariable variable;
			variable.name = match[NAME_MATCH];

			if (!FindUniformType(match[TYPE_MATCH].str(), variable.type) || IsEngineUniform(variable.name))
				continue;

			// A uniform both stages declare is still one variable.
			const bool seen = std::any_of(
				variables.begin(),
				variables.end(),
				[&](const ShaderVariable& other) { return other.name == variable.name; });

			if (seen)
				continue;

			variable.scope = match[TAG_MATCH] == INSTANCE_TAG ? UniformScope::Instance : UniformScope::Material;

			variables.push_back(variable);
		};

		return variables;
	};

	bool DeclareShaderVariable(const std::string& path, const std::string& replacing, const ShaderVariable& variable, std::string& error)
	{
		if (!IsIdentifier(variable.name) || variable.name.starts_with(RESERVED_PREFIX))
		{
			error = "'" + variable.name + "' is not a name GLSL allows";
			return false;
		};

		if (IsEngineUniform(variable.name))
		{
			error = variable.name + " is set by the engine";
			return false;
		};

		ShaderFile file;

		if (!file.Read(path, error))
			return false;

		if (!file.Declaring(variable.name).empty() && variable.name != replacing)
		{
			error = path + " already declares " + variable.name;
			return false;
		};

		const std::vector<size_t> replaced = replacing.empty() ? std::vector<size_t>{ } : file.Declaring(replacing);

		for (const size_t line : replaced)
		{
			std::smatch match;
			std::regex_match(file.lines[line], match, DeclarationPattern());

			file.lines[line] = match[1].str() + Declaration(variable);
		};

		if (replaced.empty())
		{
			// Apart, since finding the point can add lines.
			const size_t at = file.InsertionPoint();
			file.lines.insert(file.lines.begin() + at, Declaration(variable));
		};

		return file.Write(path, error);
	};

	bool RemoveShaderVariable(const std::string& path, const std::string& name, std::string& error)
	{
		ShaderFile file;

		if (!file.Read(path, error))
			return false;

		const std::vector<size_t> declaring = file.Declaring(name);

		if (declaring.empty())
		{
			error = path + " does not declare " + name;
			return false;
		};

		// From the last, so the lines still to go keep their places.
		for (auto line = declaring.rbegin(); line != declaring.rend(); line++)
			file.lines.erase(file.lines.begin() + *line);

		return file.Write(path, error);
	};

	void ApplyShaderVariables(uint32_t program, const std::vector<ShaderVariable>& variables, const UniformValues& material, const UniformValues* instance)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		for (const ShaderVariable& variable : variables)
		{
			const UniformValues* values = variable.scope == UniformScope::Instance ? instance : &material;
			const UniformValue* value = values ? values->Find(variable) : nullptr;
			const UniformTypeInfo& info = InfoOf(variable.type);

			if (info.kind == UniformKind::Texture)
			{
				renderer->SetTexture(program, variable.name.c_str(), TextureFor(*renderer, value));
				continue;
			};

			const auto& components = (value ? *value : DefaultOf(variable.type)).components;
			std::array<uint32_t, MAX_UNIFORM_COMPONENTS> packed{ };

			for (int i = 0; i < info.columns * info.rows; i++)
				switch (info.kind)
				{
				case UniformKind::Float:	packed[i] = std::bit_cast<uint32_t>((float)components[i]);	break;
				case UniformKind::Int:	packed[i] = (uint32_t)(int32_t)components[i];	break;
				case UniformKind::UInt:	packed[i] = (uint32_t)components[i];			break;
				case UniformKind::Bool:	packed[i] = components[i] != 0.0;				break;
				case UniformKind::Texture:												break;
				};

			renderer->SetUniform(program, variable.name.c_str(), variable.type, packed.data());
		};
	};

	bool DrawShaderVariables(const std::vector<ShaderVariable>& variables, UniformScope scope, UniformValues& values)
	{
		float column = 0.0f;

		for (const ShaderVariable& variable : variables)
			if (variable.scope == scope)
				column = std::max(column, ImGui::CalcTextSize(variable.name.c_str()).x);

		column += ImGui::GetStyle().ItemSpacing.x;

		bool changed = false;

		for (const ShaderVariable& variable : variables)
		{
			if (variable.scope != scope)
				continue;

			ImGui::PushID(variable.name.c_str());

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(variable.name.c_str());
			ImGui::SetItemTooltip("%s", InfoOf(variable.type).glsl);
			ImGui::SameLine(column);

			const UniformValue* found = values.Find(variable);
			UniformValue value = found ? *found : UniformValue::Default(variable.type);
			const UniformTypeInfo& info = InfoOf(variable.type);

			bool edited = false;

			switch (info.kind)
			{
			case UniformKind::Bool:		edited = DrawBools(info, value);	break;
			case UniformKind::Texture:	edited = DrawTexture(value);		break;
			default:					edited = DrawNumbers(info, value);	break;
			};

			if (edited)
			{
				values.values[variable.name] = value;
				changed = true;
			};

			ImGui::PopID();
		};

		return changed;
	};
};

#include "SerializedField.h"

#include "GameObject.h"
#include "LoomObject.h"
#include "Scene.h"
#include "ShaderVariables.h"

#include "Utilities/StateReference.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>


namespace Loom
{
	namespace
	{
		#pragma region Writing

		constexpr size_t NUMBER_LENGTH = 64;

		// Shorter arrays write quickly enough not to keep.
		constexpr size_t KEPT_ARRAY_LENGTH = 1024;

		// %g with enough digits to round-trip, so a value written and read back
		// is the same value rather than nearly the same one. to_chars writes
		// what snprintf would, many times faster, which a scene holding meshes
		// of a million floats needs.
		template <typename T>
		void AppendNumber(std::string& text, T value)
		{
			char buffer[NUMBER_LENGTH];

			const std::to_chars_result written = std::to_chars(
				std::begin(buffer),
				std::end(buffer),
				value,
				std::chars_format::general,
				std::numeric_limits<T>::max_digits10);

			text.append(buffer, written.ptr);
		};

		template <typename T>
		std::string Number(T value)
		{
			std::string text;
			AppendNumber(text, value);
			return text;
		};

		std::string Quote(const std::string& text)
		{
			std::string quoted = "\"";

			for (const char c : text)
				switch (c)
				{
				case '"':	quoted += "\\\"";	break;
				case '\\':	quoted += "\\\\";	break;
				case '\n':	quoted += "\\n";	break;
				case '\t':	quoted += "\\t";	break;
				default:	quoted += c;		break;
				};

			return quoted + '"';
		};

		// "(x, y, z)" for however many components the field has.
		std::string WriteVector(const float* values, int count)
		{
			std::string text = "(";

			for (int i = 0; i < count; i++)
			{
				if (i)
					text += ", ";

				AppendNumber(text, values[i]);
			};

			return text + ')';
		};

		#pragma endregion

		#pragma region Reading

		std::string Unquote(const std::string& text)
		{
			if (text.size() < 2 || text.front() != '"' || text.back() != '"')
				return text;

			std::string unquoted;

			for (size_t i = 1; i + 1 < text.size(); i++)
			{
				if (text[i] != '\\' || i + 2 >= text.size())
				{
					unquoted += text[i];
					continue;
				};

				switch (text[++i])
				{
				case 'n':	unquoted += '\n';	break;
				case 't':	unquoted += '\t';	break;
				default:	unquoted += text[i];break;
				};
			};

			return unquoted;
		};

		// All of text as an integer T holds, into data, or false with data as it was.
		template <typename T>
		bool ReadInteger(const std::string& text, void* data)
		{
			const char* const end = text.data() + text.size();
			T value{ };
			const std::from_chars_result read = std::from_chars(text.data(), end, value);

			if (read.ec != std::errc() || read.ptr != end)
				return false;

			*(T*)data = value;

			return true;
		};

		// As ReadInteger, by strtof and strtod: the web's libc++ has no
		// floating point from_chars.
		template <typename T>
		bool ReadReal(const std::string& text, void* data)
		{
			char* end = nullptr;
			T value{ };

			if constexpr (std::is_same_v<T, float>)
				value = strtof(text.c_str(), &end);
			else
				value = strtod(text.c_str(), &end);

			if (text.empty() || end != text.c_str() + text.size())
				return false;

			*(T*)data = value;

			return true;
		};

		bool ReadVector(const std::string& text, float* values, int count)
		{
			constexpr int MOST_COMPONENTS = 4;

			size_t at = text.find('(');

			if (at == std::string::npos)
				return false;

			float read[MOST_COMPONENTS] = { };

			for (int i = 0; i < count; i++)
			{
				const size_t start = ++at;

				while (at < text.size() && text[at] != ',' && text[at] != ')')
					at++;

				if (start >= text.size())
					return false;

				char* end = nullptr;
				const std::string component = text.substr(start, at - start);
				read[i] = strtof(component.c_str(), &end);

				if (end == component.c_str())
					return false;
			};

			std::copy(read, read + count, values);

			return true;
		};

		#pragma endregion
	};

	#pragma region Names

	std::string_view NameDisplay(std::string_view name)
	{
		const size_t slash = name.rfind('/');

		return slash == std::string_view::npos ? name : name.substr(slash + 1);
	};

	std::string_view NameCategory(std::string_view name)
	{
		const size_t slash = name.rfind('/');

		return slash == std::string_view::npos ? std::string_view() : name.substr(0, slash);
	};

	std::string_view SerializedField::Display() const
	{
		return NameDisplay(name);
	};

	std::string_view SerializedField::Category() const
	{
		return NameCategory(name);
	};

	bool SerializedField::AcceptsFile(std::string_view file) const
	{
		const auto ends_in = [file](const std::string& extension)
		{
			return
				file.size() >= extension.size() &&
				std::equal(
					extension.begin(),
					extension.end(),
					file.end() - extension.size(),
					[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
		};

		return std::any_of(extensions.begin(), extensions.end(), ends_in);
	};

	std::string NameFromIdentifier(const char* identifier)
	{
		constexpr std::string_view MEMBER_PREFIX = "m_";

		std::string_view rest = identifier;

		if (rest.starts_with(MEMBER_PREFIX))
			rest.remove_prefix(MEMBER_PREFIX.size());

		std::string name;
		bool word_start = true;
		char previous = '\0';

		for (const char c : rest)
		{
			if (c == '_')
			{
				word_start = true;
				continue;
			};

			// camelCase breaks before an upper case letter, but an acronym like ID
			// stays one word.
			if (isupper((unsigned char)c) && islower((unsigned char)previous))
				word_start = true;

			if (word_start && !name.empty())
				name += ' ';

			name += word_start ? (char)toupper((unsigned char)c) : c;

			word_start = false;
			previous = c;
		};

		return name;
	};

	#pragma endregion

	#pragma region Text

	std::string SerializedField::Write() const
	{
		if (data == nullptr)
			return "null";

		switch (type)
		{
		case FieldType::Bool:	return *(bool*)data ? "true" : "false";
		case FieldType::Int:	return std::to_string(*(int*)data);
		case FieldType::UInt:	return std::to_string(*(unsigned int*)data);
		case FieldType::Int64:	return std::to_string(*(long long*)data);
		case FieldType::UInt64:	return std::to_string(*(unsigned long long*)data);
		case FieldType::Float:	return Number(*(float*)data);
		case FieldType::Double:	return Number(*(double*)data);
		case FieldType::String:
		case FieldType::MultilineText:
		case FieldType::Asset:	return Quote(*(std::string*)data);
		case FieldType::Vec2:	return WriteVector((float*)data, 2);
		case FieldType::Vec3:	return WriteVector((float*)data, 3);
		case FieldType::Vec4:	return WriteVector((float*)data, 4);

		case FieldType::FloatArray:	return WriteArray(*(std::vector<float>*)data);
		case FieldType::UIntArray:	return WriteArray(*(std::vector<unsigned int>*)data);

		case FieldType::Uniforms:	return Quote(((UniformValues*)data)->Write());

		case FieldType::State:
		{
			const std::string& name = ((StateReference*)data)->StateType();

			return name.empty()
				? "null"
				: Quote(name);
		};

		case FieldType::Reference:
		{
			const LoomObject* target = GetReference();

			return target
				? target->GetGuid().ToString()
				: "null";
		};
		};

		return "null";
	};

	void SerializedField::Changed() const
	{
		if (version != nullptr)
			(*version)++;

		SerialChanged(owner, type == FieldType::Reference);
	};

	template <typename T>
	std::string SerializedField::WriteArray(const std::vector<T>& values) const
	{
		const size_t bytes = values.size() * sizeof(T);

		// Byte for byte, since -0 and 0 compare equal but write apart.
		if (m_written &&
			m_written->bytes.size() == bytes &&
			std::memcmp(m_written->bytes.data(), values.data(), bytes) == 0)
			return m_written->text;

		std::string text = "[";

		for (size_t i = 0; i < values.size(); i++)
		{
			if (i)
				text += ", ";

			if constexpr (std::is_floating_point_v<T>)
				AppendNumber(text, values[i]);
			else
				text += std::to_string(values[i]);
		};

		text += ']';

		if (values.size() >= KEPT_ARRAY_LENGTH)
		{
			const std::byte* const first = (const std::byte*)values.data();

			m_written = std::make_shared<WrittenArray>(WrittenArray{ { first, first + bytes }, text });
		};

		return text;
	};

	template <typename T>
	bool SerializedField::ReadArray(const std::string& text, std::vector<T>& values) const
	{
		values.clear();

		for (size_t at = 0; at < text.size(); )
		{
			const size_t start = text.find_first_not_of(" \t[],", at);

			if (start == std::string::npos)
				break;

			const size_t end = text.find_first_of(" \t[],", start);

			if constexpr (std::is_floating_point_v<T>)
				values.push_back(strtof(text.substr(start, end - start).c_str(), nullptr));
			else
			{
				T value{ };

				if (!ReadInteger<T>(text.substr(start, end - start), &value))
					return false;

				values.push_back(value);
			};

			at = end == std::string::npos ? text.size() : end;
		};

		return true;
	};

	bool SerializedField::Read(const std::string& text) const
	{
		const bool read = ReadValue(text);

		if (read)
			Changed();

		return read;
	};

	bool SerializedField::ReadValue(const std::string& text) const
	{
		if (data == nullptr)
			return false;

		switch (type)
		{
		case FieldType::Bool:
			if (text != "true" && text != "1" && text != "false" && text != "0")
				return false;

			*(bool*)data = text == "true" || text == "1";
			return true;

		case FieldType::Int:	return ReadInteger<int>(text, data);
		case FieldType::UInt:	return ReadInteger<unsigned int>(text, data);
		case FieldType::Int64:	return ReadInteger<long long>(text, data);
		case FieldType::UInt64:	return ReadInteger<unsigned long long>(text, data);
		case FieldType::Float:	return ReadReal<float>(text, data);
		case FieldType::Double:	return ReadReal<double>(text, data);
		case FieldType::String:
		case FieldType::MultilineText:
		case FieldType::Asset:	*(std::string*)data = Unquote(text);					return true;
		case FieldType::Vec2:	return ReadVector(text, (float*)data, 2);
		case FieldType::Vec3:	return ReadVector(text, (float*)data, 3);
		case FieldType::Vec4:	return ReadVector(text, (float*)data, 4);

		case FieldType::FloatArray:	return ReadArray(text, *(std::vector<float>*)data);
		case FieldType::UIntArray:	return ReadArray(text, *(std::vector<unsigned int>*)data);

		case FieldType::Uniforms:	((UniformValues*)data)->Read(Unquote(text));	return true;

		case FieldType::State:
			*(StateReference*)data = text == "null"
				? StateReference{ }
				: StateReference::Named(Unquote(text));
			return true;

		case FieldType::Reference:
		{
			if (text == "null")
			{
				SetReference(nullptr);
				return true;
			};

			Guid guid;

			if (!Guid::TryParse(text, guid))
				return false;

			SetReference(LoomObject::GetByGuid(guid));

			// What actually landed, not what was found: an object of a type this
			// field cannot hold leaves it null, and saying otherwise would turn
			// that into a silently dropped value. A target that does not exist
			// yet reads the same way, which is the loader's cue to come back to
			// it once the rest of the file has been built.
			return GetReference() != nullptr;
		};
		};

		return false;
	};

	#pragma endregion

	#pragma region References

	LoomObject* SerializedField::GetReference() const
	{
		return get_reference
			? get_reference(data)
			: nullptr;
	};

	void SerializedField::SetReference(LoomObject* object) const
	{
		if (set_reference)
			set_reference(data, object);

		Changed();
	};

	LoomObject* SerializedField::ReferenceFor(LoomObject* object) const
	{
		if (accepts_reference == nullptr || object == nullptr)
			return nullptr;

		if (accepts_reference(object))
			return object;

		GameObject* gameObject = dynamic_cast<GameObject*>(object);

		if (Scene* scene = dynamic_cast<Scene*>(object))
			gameObject = &scene->GetRoot();

		if (gameObject == nullptr)
			return nullptr;

		if (accepts_reference(gameObject))
			return gameObject;

		for (ComponentBase* component : gameObject->GetComponents())
			if (accepts_reference(component))
				return component;

		return nullptr;
	};

	std::vector<LoomObject*> SerializedField::ReferencesUnder(LoomObject& top) const
	{
		std::vector<LoomObject*> found{ };

		if (accepts_reference == nullptr)
			return found;

		std::vector<GameObject*> pending{ };

		if (Scene* scene = dynamic_cast<Scene*>(&top))
		{
			if (accepts_reference(scene))
				found.push_back(scene);

			pending.push_back(&scene->GetRoot());
		}
		else if (GameObject* gameObject = dynamic_cast<GameObject*>(&top))
			pending.push_back(gameObject);

		while (!pending.empty())
		{
			GameObject* gameObject = pending.back();
			pending.pop_back();

			if (accepts_reference(gameObject))
				found.push_back(gameObject);

			for (ComponentBase* component : gameObject->GetComponents())
				if (accepts_reference(component))
					found.push_back(component);

			const std::vector<GameObject*>& children = gameObject->GetChildren();
			pending.insert(pending.end(), children.rbegin(), children.rend());
		};

		return found;
	};

	#pragma endregion
};

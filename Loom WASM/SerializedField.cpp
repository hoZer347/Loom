#include "SerializedField.h"

#include "GameObject.h"
#include "LoomObject.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>


namespace Loom
{
	namespace
	{
		// %g with enough digits to round-trip, so a value written and read back
		// is the same value rather than nearly the same one.
		std::string Number(const char* format, auto value)
		{
			char buffer[64]{ };
			snprintf(buffer, sizeof(buffer), format, value);
			return buffer;
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

		// "(x, y, z)" for however many components the field has.
		std::string WriteVector(const float* values, int count)
		{
			std::string text = "(";

			for (int i = 0; i < count; i++)
				text += (i ? ", " : "") + Number("%.9g", values[i]);

			return text + ')';
		};

		bool ReadVector(const std::string& text, float* values, int count)
		{
			size_t at = text.find('(');

			if (at == std::string::npos)
				return false;

			for (int i = 0; i < count; i++)
			{
				const size_t start = ++at;

				while (at < text.size() && text[at] != ',' && text[at] != ')')
					at++;

				if (start >= text.size())
					return false;

				values[i] = strtof(text.substr(start, at - start).c_str(), nullptr);
			};

			return true;
		};
	};

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
	};

	LoomObject* SerializedField::ReferenceFor(LoomObject* object) const
	{
		if (accepts_reference == nullptr || object == nullptr)
			return nullptr;

		if (accepts_reference(object))
			return object;

		if (const GameObject* gameObject = dynamic_cast<GameObject*>(object))
			for (ComponentBase* component : gameObject->GetComponents())
				if (accepts_reference(component))
					return component;

		return nullptr;
	};

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
		case FieldType::Float:	return Number("%.9g", *(float*)data);
		case FieldType::Double:	return Number("%.17g", *(double*)data);
		case FieldType::String:	return Quote(*(std::string*)data);
		case FieldType::Vec2:	return WriteVector((float*)data, 2);
		case FieldType::Vec3:	return WriteVector((float*)data, 3);
		case FieldType::Vec4:	return WriteVector((float*)data, 4);

		case FieldType::FloatArray:
		{
			const std::vector<float>& values = *(std::vector<float>*)data;

			std::string text = "[";

			for (size_t i = 0; i < values.size(); i++)
				text += (i ? ", " : "") + Number("%.9g", values[i]);

			return text + ']';
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

	bool SerializedField::Read(const std::string& text) const
	{
		if (data == nullptr)
			return false;

		switch (type)
		{
		case FieldType::Bool:	*(bool*)data = text == "true" || text == "1";			return true;
		case FieldType::Int:	*(int*)data = (int)strtol(text.c_str(), nullptr, 10);	return true;
		case FieldType::UInt:	*(unsigned int*)data = (unsigned int)strtoul(text.c_str(), nullptr, 10); return true;
		case FieldType::Int64:	*(long long*)data = strtoll(text.c_str(), nullptr, 10);	return true;
		case FieldType::UInt64:	*(unsigned long long*)data = strtoull(text.c_str(), nullptr, 10); return true;
		case FieldType::Float:	*(float*)data = strtof(text.c_str(), nullptr);			return true;
		case FieldType::Double:	*(double*)data = strtod(text.c_str(), nullptr);			return true;
		case FieldType::String:	*(std::string*)data = Unquote(text);					return true;
		case FieldType::Vec2:	return ReadVector(text, (float*)data, 2);
		case FieldType::Vec3:	return ReadVector(text, (float*)data, 3);
		case FieldType::Vec4:	return ReadVector(text, (float*)data, 4);

		case FieldType::FloatArray:
		{
			std::vector<float>& values = *(std::vector<float>*)data;

			values.clear();

			for (size_t at = 0; at < text.size(); )
			{
				const size_t start = text.find_first_not_of(" \t[],", at);

				if (start == std::string::npos)
					break;

				const size_t end = text.find_first_of(" \t[],", start);

				values.push_back(strtof(text.substr(start, end - start).c_str(), nullptr));

				at = end == std::string::npos ? text.size() : end;
			};

			return true;
		};

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
};

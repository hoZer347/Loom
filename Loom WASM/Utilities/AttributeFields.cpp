#include "AttributeFields.h"


namespace Loom
{
	// "&health, &attack" -> "health", "attack". The expressions HOZER_ATTRIBUTES is given
	// are addresses of members, so the leading & and any surrounding space is decoration.
	// A qualified expression (&this->health, &m_stats.health) keeps only its last hop,
	// which is the part a readout wants to print.
	static std::string CleanName(const std::string& raw)
	{
		size_t begin = 0;
		size_t end = raw.size();

		while (begin < end && (raw[begin] == ' ' || raw[begin] == '\t' || raw[begin] == '&'))
			begin++;

		while (end > begin && (raw[end - 1] == ' ' || raw[end - 1] == '\t'))
			end--;

		std::string name = raw.substr(begin, end - begin);

		const size_t arrow = name.rfind("->");
		const size_t dot = name.rfind('.');

		if (arrow != std::string::npos && (dot == std::string::npos || arrow > dot))
			name = name.substr(arrow + 2);
		else if (dot != std::string::npos)
			name = name.substr(dot + 1);

		return name;
	};

	void AttributeFields::Zip(
		std::vector<AttributeField>& into,
		const char* names,
		std::initializer_list<AttributeBase*> attributes)
	{
		const std::string list = names ? names : "";

		std::vector<std::string> split;
		std::string current;

		// Split on top-level commas only, so a templated expression -- &get<int, float>()
		// -- is one argument rather than two. Nothing else in the argument list nests.
		int depth = 0;

		for (const char c : list)
		{
			if (c == '(' || c == '[' || c == '<')
				depth++;
			else if (c == ')' || c == ']' || c == '>')
				depth--;

			if (c == ',' && depth <= 0)
			{
				split.emplace_back(current);
				current.clear();

				continue;
			};

			current += c;
		};

		if (!current.empty())
			split.emplace_back(current);

		size_t index = 0;

		for (AttributeBase* attribute : attributes)
		{
			into.push_back(
				AttributeField{
					index < split.size() ? CleanName(split[index]) : std::string(),
					attribute });

			index++;
		};
	};
};


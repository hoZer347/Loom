#include "DialogueArgs.h"

#include <cctype>
#include <cstdlib>


namespace Loom
{
	std::string DialogueArgs::Trim(const std::string& s)
	{
		size_t begin = 0;
		size_t end = s.size();

		while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
			begin++;

		while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
			end--;

		return s.substr(begin, end - begin);
	};

	std::vector<std::string> DialogueArgs::Split(const std::string& raw)
	{
		std::vector<std::string> args;
		std::string current;
		bool inQuotes = false;

		for (const char c : raw)
		{
			if (c == '"')
			{
				inQuotes = !inQuotes;
				current += c;
			}
			else if (c == ',' && !inQuotes)
			{
				args.push_back(Trim(current));
				current.clear();
			}
			else current += c;
		};

		const std::string last = Trim(current);

		// An empty tail still counts once something has been split off -- "a," is two
		// arguments, the second of them blank, and a binding that takes two should see
		// them both rather than be told it was handed one.
		if (!last.empty() || !args.empty())
			args.push_back(last);

		return args;
	};

	std::string DialogueArgs::Unquote(const std::string& s)
	{
		if (s.size() < 2 || s.front() != '"' || s.back() != '"')
			return s;

		std::string inner = s.substr(1, s.size() - 2);
		std::string unescaped;
		unescaped.reserve(inner.size());

		for (size_t i = 0; i < inner.size(); i++)
			if (inner[i] == '\\' && i + 1 < inner.size() && inner[i + 1] == '"')
			{
				unescaped += '"';
				i++;
			}
			else unescaped += inner[i];

		return unescaped;
	};

	bool DialogueArgs::TryBool(const std::string& token, bool& out)
	{
		std::string lowered;
		lowered.reserve(token.size());

		for (const char c : token)
			lowered += char(std::tolower(static_cast<unsigned char>(c)));

		if (lowered == "true")
		{
			out = true;

			return true;
		};

		if (lowered == "false")
		{
			out = false;

			return true;
		};

		return false;
	};

	bool DialogueArgs::TryInteger(const std::string& token, long long& out)
	{
		if (token.empty())
			return false;

		char* end = nullptr;
		const long long value = std::strtoll(token.c_str(), &end, 10);

		// The whole token has to be the number. "3 apples" is not a 3 -- it is a script
		// with a typo in it, and reading it as 3 hides the typo.
		if (end == nullptr || *end != '\0')
			return false;

		out = value;

		return true;
	};

	bool DialogueArgs::TryNumber(const std::string& token, double& out)
	{
		if (token.empty())
			return false;

		char* end = nullptr;
		const double value = std::strtod(token.c_str(), &end);

		if (end == nullptr || *end != '\0')
			return false;

		out = value;

		return true;
	};

	std::string DialogueArgs::StripNumericSuffix(const std::string& token)
	{
		if (token.empty())
			return token;

		const char last = char(std::tolower(static_cast<unsigned char>(token.back())));

		if (last == 'f' || last == 'd')
			return token.substr(0, token.size() - 1);

		return token;
	};
};

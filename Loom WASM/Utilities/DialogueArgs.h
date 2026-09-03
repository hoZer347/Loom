#pragma once

#include <string>
#include <type_traits>
#include <vector>


namespace Loom
{
	/// Turns the inside of a [Binding, arg, arg] token into arguments.
	struct DialogueArgs final
	{
		/// Splits on commas that are not inside quotes, trimming each argument. Quotes are
		/// kept at this stage -- Coerce strips them, and only for the types where a quote
		/// is decoration rather than content.
		static std::vector<std::string> Split(const std::string& raw);

		/// Drops a surrounding pair of double quotes and unescapes any \" inside.
		static std::string Unquote(const std::string& s);

		/// Reads one token as the requested type. False when it does not read as one,
		/// which is how an overloaded binding picks between its forms.
		template <typename T>
		static bool TryCoerce(const std::string& token, T& out)
		{
			using _Type = std::decay_t<T>;

			const std::string trimmed = Trim(token);

			if constexpr (std::is_same_v<_Type, std::string>)
			{
				out = Unquote(trimmed);

				return true;
			}
			else if constexpr (std::is_same_v<_Type, bool>)
				return TryBool(trimmed, out);
			else if constexpr (std::is_enum_v<_Type>)
			{
				long long value = 0;

				if (!TryInteger(trimmed, value))
					return false;

				out = static_cast<_Type>(value);

				return true;
			}
			else if constexpr (std::is_integral_v<_Type>)
			{
				long long value = 0;

				if (!TryInteger(trimmed, value))
					return false;

				out = static_cast<_Type>(value);

				return true;
			}
			else if constexpr (std::is_floating_point_v<_Type>)
			{
				double value = 0.0;

				// A suffixed literal (0.1f, 2d) is still a number, and a script author
				// has no reason to know what is reading it.
				if (!TryNumber(StripNumericSuffix(trimmed), value))
					return false;

				out = static_cast<_Type>(value);

				return true;
			}
			else return false;
		};

		static std::string Trim(const std::string& s);

	private:
		static bool TryBool(const std::string& token, bool& out);
		static bool TryInteger(const std::string& token, long long& out);
		static bool TryNumber(const std::string& token, double& out);
		static std::string StripNumericSuffix(const std::string& token);
	};
};

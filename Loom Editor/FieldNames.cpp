#include "FieldNames.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "SerializedField.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// For dbghelp.h's own copy of the symbol tags, there being no cvconst.h here.
#define _NO_CVCONST_H
#include <dbghelp.h>

#include <cctype>
#include <map>
#include <mutex>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#pragma comment(lib, "dbghelp.lib")


namespace Loom
{
	namespace
	{
		// How a Serial member's type is spelled in the symbols, up to its argument.
		constexpr std::wstring_view serial_type = L"Loom::Serial<";

		// cvconst.h's DataIsMember, which dbghelp.h leaves out of its copy of
		// the symbol enums.
		constexpr DWORD data_is_member = 7;

		// A member's label by its offset in the complete object.
		typedef std::map<ptrdiff_t, std::string> Members;

		// Not a process handle: DbgHelp takes any unique value for a session that
		// loads its modules by hand, which keeps this one apart from anything
		// else in the process using DbgHelp.
		HANDLE Session()
		{
			static const char session = 0;
			static const bool initialised = SymInitializeW((HANDLE)&session, nullptr, FALSE);

			return initialised ? (HANDLE)&session : nullptr;
		};

		std::string Narrow(const std::wstring_view text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);

			std::string narrow(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), narrow.data(), size, nullptr, nullptr);

			return narrow;
		};

		template <typename T>
		bool TypeInfo(DWORD64 base, ULONG type, IMAGEHLP_SYMBOL_TYPE_INFO what, T& value)
		{
			return SymGetTypeInfo(Session(), base, type, what, &value);
		};

		std::wstring SymbolName(DWORD64 base, ULONG type)
		{
			WCHAR* name = nullptr;

			if (!TypeInfo(base, type, TI_GET_SYMNAME, name) || name == nullptr)
				return L"";

			std::wstring text = name;
			LocalFree(name);

			return text;
		};

		// The Serial members of a type and of its non-virtual bases, by where
		// they sit in a complete object whose copy of the type is at offset.
		void Collect(DWORD64 base, ULONG type, ptrdiff_t offset, Members& members)
		{
			DWORD count = 0;

			if (!TypeInfo(base, type, TI_GET_CHILDRENCOUNT, count) || count == 0)
				return;

			std::vector<BYTE> buffer(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
			TI_FINDCHILDREN_PARAMS& children = *(TI_FINDCHILDREN_PARAMS*)buffer.data();

			children.Count = count;

			if (!SymGetTypeInfo(Session(), base, type, TI_FINDCHILDREN, &children))
				return;

			for (DWORD i = 0; i < count; i++)
			{
				const ULONG child = children.ChildId[i];

				DWORD tag = 0;
				DWORD child_offset = 0;
				ULONG child_type = 0;

				if (!TypeInfo(base, child, TI_GET_SYMTAG, tag) ||
					!TypeInfo(base, child, TI_GET_OFFSET, child_offset) ||
					!TypeInfo(base, child, TI_GET_TYPEID, child_type))
					continue;

				if (tag == SymTagBaseClass)
				{
					BOOL is_virtual = FALSE;
					TypeInfo(base, child, TI_GET_VIRTUALBASECLASS, is_virtual);

					if (!is_virtual)
						Collect(base, child_type, offset + child_offset, members);
				}
				else if (tag == SymTagData)
				{
					DWORD kind = 0;
					DWORD type_tag = 0;

					if (!TypeInfo(base, child, TI_GET_DATAKIND, kind) ||
						kind != data_is_member ||
						!TypeInfo(base, child_type, TI_GET_SYMTAG, type_tag) ||
						type_tag != SymTagUDT)
						continue;

					// A struct member can hold Serials of its own, which belong to
					// this object all the same.
					if (SymbolName(base, child_type).starts_with(serial_type))
						members[offset + child_offset] = NameFromIdentifier(Narrow(SymbolName(base, child)).c_str());
					else Collect(base, child_type, offset + child_offset, members);
				};
			};
		};

		// typeid spells a type "struct Game::Name<class Game::Other>" and puts an
		// anonymous namespace in words; the symbols say "Game::Name<Game::Other>"
		// and hyphenate it.
		std::string SymbolTypeName(std::string name)
		{
			const std::pair<std::string_view, std::string_view> spellings[] =
			{
				{ "struct ", "" },
				{ "class ", "" },
				{ "enum ", "" },
				{ "`anonymous namespace'", "`anonymous-namespace'" },
			};

			for (const auto& [from, to] : spellings)
				for (size_t at = name.find(from); at != std::string::npos; at = name.find(from, at))
				{
					// The tail of a longer identifier, as in "Subclass ", is left be.
					if (at > 0 && (isalnum((unsigned char)name[at - 1]) || name[at - 1] == '_'))
					{
						at += from.size();
						continue;
					};

					name.replace(at, from.size(), to);
					at += to.size();
				};

			return name;
		};

		// What the symbols of the module that made this object say its Serial
		// members are called. Empty when they cannot say.
		Members Read(const std::type_info& type, HMODULE module, const IMAGE_NT_HEADERS& headers)
		{
			if (Session() == nullptr)
				return { };

			std::wstring path(UNICODE_STRING_MAX_CHARS, L'\0');
			path.resize(GetModuleFileNameW(module, path.data(), (DWORD)path.size()));

			DWORD64 base = SymLoadModuleExW(
				Session(), nullptr, path.c_str(), nullptr,
				(DWORD64)module, headers.OptionalHeader.SizeOfImage, nullptr, 0);

			// Already loaded, which the editor's own module stays.
			if (base == 0 && GetLastError() == ERROR_SUCCESS)
				base = (DWORD64)module;

			if (base == 0)
				return { };

			const std::string name = SymbolTypeName(type.name());

			std::vector<BYTE> buffer(sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR));
			SYMBOL_INFOW& symbol = *(SYMBOL_INFOW*)buffer.data();

			symbol.SizeOfStruct = sizeof(SYMBOL_INFOW);
			symbol.MaxNameLen = MAX_SYM_NAME;

			const std::wstring wide(name.begin(), name.end());

			Members members;

			if (SymGetTypeFromNameW(Session(), base, wide.c_str(), &symbol))
				Collect(base, symbol.TypeIndex, 0, members);

			// A script library's is unloaded straight away: DbgHelp holds the .pdb
			// open while it is loaded, and every build of the scripts rewrites it.
			// The editor's own is kept for the next type, it being most of them.
			if (module != GetModuleHandleW(nullptr))
				SymUnloadModule64(Session(), base);

			return members;
		};
	};

	std::vector<std::string> FieldNames::Of(const LoomObject& object)
	{
		static std::mutex mutex;
		static std::map<std::pair<const std::type_info*, DWORD>, Members> types;

		const std::type_info& type = typeid(object);

		// The type's RTTI lives in the module that built its vtable, whose
		// symbols describe it.
		HMODULE module = nullptr;

		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&type,
			&module);

		const IMAGE_NT_HEADERS* headers = module ? ImageNtHeader(module) : nullptr;

		std::lock_guard lock{ mutex };

		// The build's timestamp is in the key: a reloaded library can put a type
		// with a new layout where the old one's RTTI was.
		const auto key = std::make_pair(&type, headers ? headers->FileHeader.TimeDateStamp : 0);

		auto found = types.find(key);

		if (found == types.end())
			found = types.emplace(key, headers ? Read(type, module, *headers) : Members{ }).first;

		const char* const complete = (const char*)dynamic_cast<const void*>(&object);

		std::vector<std::string> names;

		for (const SerializedField& field : object.GetFields())
		{
			const auto member = found->second.find((const char*)field.data - complete);

			names.push_back(member != found->second.end()
				? member->second
				: "Field " + std::to_string(names.size()));
		};

		return names;
	};
};

#endif

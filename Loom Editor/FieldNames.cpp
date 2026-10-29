#include "FieldNames.h"

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "SerializedField.h"

#include <cctype>
#include <map>
#include <mutex>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// For dbghelp.h's own copy of the symbol tags, there being no cvconst.h here.
#define _NO_CVCONST_H
#include <dbghelp.h>
#include <oaidl.h>

#pragma comment(lib, "dbghelp.lib")

#else

#include <algorithm>
#include <cstdlib>
#include <memory>

#include <cxxabi.h>
#include <dlfcn.h>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#endif


namespace Loom
{
	namespace
	{
		struct Member final
		{
			std::string label;
			std::vector<FieldNames::Enumerator> enumerators;
		};

		// A member by its offset in the complete object.
		typedef std::map<ptrdiff_t, Member> Members;

		// One per field, null for a field none of the members is at.
		std::vector<const Member*> Match(const LoomObject& object, const Members& found)
		{
			const char* const complete = (const char*)dynamic_cast<const void*>(&object);

			std::vector<const Member*> members;

			for (const SerializedField& field : object.GetFields())
			{
				const auto member = found.find((const char*)field.data - complete);

				members.push_back(member != found.end() ? &member->second : nullptr);
			};

			return members;
		};

#ifdef _WIN32

		#pragma region Symbols

		// DbgHelp takes any unique value as a session, keeping this apart from other users of it.
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

		std::vector<ULONG> Children(DWORD64 base, ULONG type)
		{
			DWORD count = 0;

			if (!TypeInfo(base, type, TI_GET_CHILDRENCOUNT, count) || count == 0)
				return { };

			std::vector<BYTE> buffer(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
			TI_FINDCHILDREN_PARAMS& children = *(TI_FINDCHILDREN_PARAMS*)buffer.data();

			children.Count = count;

			if (!SymGetTypeInfo(Session(), base, type, TI_FINDCHILDREN, &children))
				return { };

			return std::vector<ULONG>(children.ChildId, children.ChildId + count);
		};

		// An enumerator's value comes as the smallest integer type that fits.
		bool Integer(const VARIANT& variant, long long& value)
		{
			switch (variant.vt)
			{
			case VT_I1:		value = variant.cVal;		return true;
			case VT_I2:		value = variant.iVal;		return true;
			case VT_I4:		value = variant.lVal;		return true;
			case VT_I8:		value = variant.llVal;		return true;
			case VT_INT:	value = variant.intVal;		return true;
			case VT_UI1:	value = variant.bVal;		return true;
			case VT_UI2:	value = variant.uiVal;		return true;
			case VT_UI4:	value = variant.ulVal;		return true;
			case VT_UI8:	value = variant.ullVal;		return true;
			case VT_UINT:	value = variant.uintVal;	return true;
			default:		return false;
			};
		};

		#pragma endregion

		#pragma region Members

		// How a Serial member's type is spelled in the symbols, up to its argument.
		constexpr std::wstring_view serial_type = L"Loom::Serial<";

		// cvconst.h's DataIsMember, which dbghelp.h leaves out.
		constexpr DWORD data_is_member = 7;

		std::vector<FieldNames::Enumerator> Enumerators(DWORD64 base, ULONG serial)
		{
			std::vector<FieldNames::Enumerator> enumerators;

			for (const ULONG member : Children(base, serial))
			{
				DWORD kind = 0;
				ULONG value_type = 0;
				DWORD value_tag = 0;

				if (!TypeInfo(base, member, TI_GET_DATAKIND, kind) ||
					kind != data_is_member ||
					!TypeInfo(base, member, TI_GET_TYPEID, value_type) ||
					!TypeInfo(base, value_type, TI_GET_SYMTAG, value_tag) ||
					value_tag != SymTagEnum)
					continue;

				for (const ULONG enumerator : Children(base, value_type))
				{
					VARIANT variant{ };
					long long value = 0;

					if (TypeInfo(base, enumerator, TI_GET_VALUE, variant) && Integer(variant, value))
						enumerators.push_back({ NameFromIdentifier(Narrow(SymbolName(base, enumerator)).c_str()), value });
				};
			};

			return enumerators;
		};

		// Of the type and its non-virtual bases, offset as in a complete object.
		void Collect(DWORD64 base, ULONG type, ptrdiff_t offset, Members& members)
		{
			for (const ULONG child : Children(base, type))
			{
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

					// A struct member's own Serials belong to this object too.
					if (SymbolName(base, child_type).starts_with(serial_type))
						members[offset + child_offset] =
						{
							NameFromIdentifier(Narrow(SymbolName(base, child)).c_str()),
							Enumerators(base, child_type),
						};
					else Collect(base, child_type, offset + child_offset, members);
				};
			};
		};

		#pragma endregion

		#pragma region Lookup

		// typeid writes "struct Game::Name<class Game::Other>"; the symbols drop the keywords.
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

		// Empty when the symbols cannot say.
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

			// DbgHelp holds a script library's .pdb open, and every build rewrites it.
			if (module != GetModuleHandleW(nullptr))
				SymUnloadModule64(Session(), base);

			return members;
		};

		// Null where the symbols say nothing.
		std::vector<const Member*> Lookup(const LoomObject& object)
		{
			static std::mutex mutex;
			static std::map<std::pair<const std::type_info*, DWORD>, Members> types;

			const std::type_info& type = typeid(object);

			// The type's RTTI lives in the module that built its vtable.
			HMODULE module = nullptr;

			GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)&type,
				&module);

			const IMAGE_NT_HEADERS* headers = module ? ImageNtHeader(module) : nullptr;

			std::lock_guard lock{ mutex };

			// A reloaded library can put a new layout where the old RTTI was.
			const auto key = std::make_pair(&type, headers ? headers->FileHeader.TimeDateStamp : 0);

			auto found = types.find(key);

			if (found == types.end())
				found = types.emplace(key, headers ? Read(type, module, *headers) : Members{ }).first;

			return Match(object, found->second);
		};

		#pragma endregion

#else

		#pragma region Debug info

		// How a Serial member's type is named in the debug info, up to its argument.
		constexpr std::string_view serial_type = "Serial<";

		// Namespaces whose types never hold a Serial, left out of the index.
		constexpr std::string_view unindexed[] = { "std", "__gnu_cxx", "__cxxabiv1" };

		std::string_view Name(Dwarf_Die& die)
		{
			const char* name = dwarf_diename(&die);

			return name != nullptr ? name : "";
		};

		// Through typedefs and qualifiers to the type itself.
		bool TypeOf(Dwarf_Die& die, Dwarf_Die& type)
		{
			Dwarf_Attribute attribute;

			if (dwarf_attr_integrate(&die, DW_AT_type, &attribute) == nullptr || dwarf_formref_die(&attribute, &type) == nullptr)
				return false;

			for (int tag = dwarf_tag(&type); tag == DW_TAG_typedef || tag == DW_TAG_const_type || tag == DW_TAG_volatile_type; tag = dwarf_tag(&type))
				if (dwarf_attr_integrate(&type, DW_AT_type, &attribute) == nullptr || dwarf_formref_die(&attribute, &type) == nullptr)
					return false;

			return true;
		};

		bool IsRecord(Dwarf_Die& die)
		{
			const int tag = dwarf_tag(&die);

			return tag == DW_TAG_structure_type || tag == DW_TAG_class_type;
		};

		// A virtual base's offset is an expression rather than a constant, so it has none here.
		bool Offset(Dwarf_Die& die, Dwarf_Word& offset)
		{
			Dwarf_Attribute attribute;

			return dwarf_attr(&die, DW_AT_data_member_location, &attribute) != nullptr && dwarf_formudata(&attribute, &offset) == 0;
		};

		// Its enumerators' values are stored with no sign of their own, so this tells how to read them.
		bool IsUnsigned(Dwarf_Die& enumeration)
		{
			Dwarf_Die underlying;
			Dwarf_Attribute attribute;
			Dwarf_Word encoding = 0;

			return TypeOf(enumeration, underlying) &&
				dwarf_attr(&underlying, DW_AT_encoding, &attribute) != nullptr &&
				dwarf_formudata(&attribute, &encoding) == 0 &&
				(encoding == DW_ATE_unsigned || encoding == DW_ATE_unsigned_char || encoding == DW_ATE_boolean);
		};

		bool Value(Dwarf_Die& enumerator, bool is_unsigned, long long& value)
		{
			Dwarf_Attribute attribute;

			if (dwarf_attr(&enumerator, DW_AT_const_value, &attribute) == nullptr)
				return false;

			Dwarf_Word unsigned_value = 0;
			Dwarf_Sword signed_value = 0;

			if (is_unsigned ? dwarf_formudata(&attribute, &unsigned_value) != 0 : dwarf_formsdata(&attribute, &signed_value) != 0)
				return false;

			value = is_unsigned ? (long long)unsigned_value : (long long)signed_value;

			return true;
		};

		// A module's debug info, with each struct or class it defines by its qualified name.
		struct Module final
		{
			int file = -1;
			Dwarf* dwarf = nullptr;
			std::map<std::string, Dwarf_Off, std::less<>> types;

			explicit Module(const std::string& path)
			{
				file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
				dwarf = file >= 0 ? dwarf_begin(file, DWARF_C_READ) : nullptr;

				if (dwarf == nullptr)
					return;

				Dwarf_CU* unit = nullptr;
				Dwarf_Die root;

				while (dwarf_get_units(dwarf, unit, &unit, nullptr, nullptr, &root, nullptr) == 0)
					Index(root, "");
			};

			~Module()
			{
				if (dwarf != nullptr)
					dwarf_end(dwarf);

				if (file >= 0)
					close(file);
			};

			Module(const Module&) = delete;
			Module& operator=(const Module&) = delete;

			// Named as the demangler names them, so typeid finds them.
			void Index(Dwarf_Die& scope, const std::string& prefix)
			{
				Dwarf_Die child;

				if (dwarf_child(&scope, &child) != 0)
					return;

				do
				{
					const std::string_view name = Name(child);

					if (dwarf_tag(&child) == DW_TAG_namespace)
					{
						if (prefix.empty() && std::ranges::find(unindexed, name) != std::end(unindexed))
							continue;

						Index(child, prefix + std::string(name.empty() ? "(anonymous namespace)" : name) + "::");
					}
					else if (IsRecord(child) && !name.empty())
					{
						const std::string qualified = prefix + std::string(name);

						if (!dwarf_hasattr(&child, DW_AT_declaration))
							types.try_emplace(qualified, dwarf_dieoffset(&child));

						Index(child, qualified + "::");
					};
				}
				while (dwarf_siblingof(&child, &child) == 0);
			};
		};

		#pragma endregion

		#pragma region Members

		std::vector<FieldNames::Enumerator> Enumerators(Dwarf_Die& serial)
		{
			std::vector<FieldNames::Enumerator> enumerators;
			Dwarf_Die member;

			if (dwarf_child(&serial, &member) != 0)
				return enumerators;

			do
			{
				Dwarf_Die value_type;
				Dwarf_Die enumerator;

				if (dwarf_tag(&member) != DW_TAG_member ||
					dwarf_hasattr(&member, DW_AT_declaration) ||
					!TypeOf(member, value_type) ||
					dwarf_tag(&value_type) != DW_TAG_enumeration_type ||
					dwarf_child(&value_type, &enumerator) != 0)
					continue;

				const bool is_unsigned = IsUnsigned(value_type);

				do
				{
					long long value = 0;

					if (dwarf_tag(&enumerator) == DW_TAG_enumerator && Value(enumerator, is_unsigned, value))
						enumerators.push_back({ NameFromIdentifier(std::string(Name(enumerator)).c_str()), value });
				}
				while (dwarf_siblingof(&enumerator, &enumerator) == 0);
			}
			while (dwarf_siblingof(&member, &member) == 0);

			return enumerators;
		};

		// Of the type and its non-virtual bases, offset as in a complete object.
		void Collect(Dwarf_Die& type, ptrdiff_t offset, Members& members)
		{
			Dwarf_Die child;

			if (dwarf_child(&type, &child) != 0)
				return;

			do
			{
				const int tag = dwarf_tag(&child);
				Dwarf_Die child_type;
				Dwarf_Word child_offset = 0;

				if ((tag != DW_TAG_inheritance && tag != DW_TAG_member) ||
					!Offset(child, child_offset) ||
					!TypeOf(child, child_type) ||
					!IsRecord(child_type))
					continue;

				const ptrdiff_t at = offset + (ptrdiff_t)child_offset;

				// A struct member's own Serials belong to this object too.
				if (tag == DW_TAG_member && Name(child_type).starts_with(serial_type))
					members[at] = { NameFromIdentifier(std::string(Name(child)).c_str()), Enumerators(child_type) };
				else Collect(child_type, at, members);
			}
			while (dwarf_siblingof(&child, &child) == 0);
		};

		#pragma endregion

		#pragma region Lookup

		// typeid gives the mangled name, and the debug info spells it as the demangler does.
		std::string Demangled(const char* mangled)
		{
			int status = 0;
			const std::unique_ptr<char, decltype(&free)> name{ abi::__cxa_demangle(mangled, nullptr, nullptr, &status), &free };

			return name ? std::string(name.get()) : std::string(mangled);
		};

		Members Read(const std::type_info& type, Module& module)
		{
			Members members;
			const auto found = module.types.find(Demangled(type.name()));
			Dwarf_Die die;

			if (found != module.types.end() && dwarf_offdie(module.dwarf, found->second, &die) != nullptr)
				Collect(die, 0, members);

			return members;
		};

		// Null where the debug info says nothing.
		std::vector<const Member*> Lookup(const LoomObject& object)
		{
			static std::mutex mutex;
			static std::map<std::pair<const std::type_info*, std::string>, Members> types;
			static std::unique_ptr<Module> executable;

			const std::type_info& type = typeid(object);

			// The type's RTTI lives in the module that built its vtable.
			Dl_info module{ };
			Dl_info self{ };

			dladdr(&type, &module);
			dladdr(reinterpret_cast<void*>(&FieldNames::Of), &self);

			// The executable's own name comes as it was started, which may be relative.
			const bool is_executable = module.dli_fname == nullptr || module.dli_fbase == self.dli_fbase;
			const std::string path = is_executable ? "/proc/self/exe" : module.dli_fname;

			// A reloaded library can put a new layout where the old RTTI was.
			struct stat status{ };
			stat(path.c_str(), &status);

			const auto key = std::make_pair(&type, path + '@' + std::to_string(status.st_mtim.tv_sec) + '.' + std::to_string(status.st_mtim.tv_nsec));

			std::lock_guard lock{ mutex };

			auto found = types.find(key);

			if (found == types.end())
			{
				if (is_executable && !executable)
					executable = std::make_unique<Module>(path);

				if (is_executable)
					found = types.emplace(key, Read(type, *executable)).first;
				else
				{
					// Only the executable's index is kept, since every script build is a new library.
					Module library{ path };
					found = types.emplace(key, Read(type, library)).first;
				};
			};

			return Match(object, found->second);
		};

		#pragma endregion

#endif
	};

	std::vector<std::string> FieldNames::Of(const LoomObject& object)
	{
		std::vector<std::string> names;
		const std::vector<SerializedField>& fields = object.GetFields();

		for (const Member* member : Lookup(object))
		{
			const std::string_view display = fields[names.size()].Display();

			names.push_back(
				!display.empty() ? std::string(display) :
				member ? member->label :
				"Field " + std::to_string(names.size()));
		};

		return names;
	};

	std::vector<const std::vector<FieldNames::Enumerator>*> FieldNames::EnumeratorsOf(const LoomObject& object)
	{
		std::vector<const std::vector<Enumerator>*> enumerators;

		for (const Member* member : Lookup(object))
			enumerators.push_back(member && !member->enumerators.empty() ? &member->enumerators : nullptr);

		return enumerators;
	};
};

#endif

#include "DialogueBinding.h"

#include "Serial.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		std::mutex& Mutex()
		{
			static std::mutex mutex;
			return mutex;
		};

		std::unordered_map<const LoomObject*, std::vector<DialogueBindings::Form>>& Declared()
		{
			static std::unordered_map<const LoomObject*, std::vector<DialogueBindings::Form>> declared;
			return declared;
		};

		std::map<std::string, int>& NameCounts()
		{
			static std::map<std::string, int> counts;
			return counts;
		};

		std::atomic<unsigned long long> names_version{ 0 };
	};

	#pragma region Bindings

	LoomObject* DialogueBindings::Register(const char* name, const Invoke* invoke)
	{
		LoomObject* owner = ObjectUnderConstruction();

		if (owner == nullptr)
		{
			std::cerr
				<< "A DialogueBinding has no LoomObject under construction on this"
				   " thread to belong to" << std::endl;

			return nullptr;
		};

		std::lock_guard lock{ Mutex() };

		Declared()[owner].push_back({ name, invoke });

		return owner;
	};

	void DialogueBindings::Unregister(const LoomObject* owner, const Invoke* invoke)
	{
		std::lock_guard lock{ Mutex() };

		const auto found = Declared().find(owner);

		if (found == Declared().end())
			return;

		std::erase_if(found->second, [invoke](const Form& form) { return form.invoke == invoke; });

		if (found->second.empty())
			Declared().erase(found);
	};

	std::vector<DialogueBindings::Form> DialogueBindings::Of(const LoomObject* owner)
	{
		std::lock_guard lock{ Mutex() };

		const auto found = Declared().find(owner);

		return found == Declared().end() ? std::vector<Form>() : found->second;
	};

	#pragma endregion

	#pragma region Names

	void DialogueBindings::AddName(const char* name)
	{
		std::lock_guard lock{ Mutex() };

		if (NameCounts()[name]++ == 0)
			names_version++;
	};

	void DialogueBindings::RemoveName(const char* name)
	{
		std::lock_guard lock{ Mutex() };

		const auto found = NameCounts().find(name);

		if (found == NameCounts().end() || --found->second > 0)
			return;

		NameCounts().erase(found);
		names_version++;
	};

	std::vector<std::string> DialogueBindings::Names()
	{
		std::lock_guard lock{ Mutex() };

		std::vector<std::string> names;

		for (const auto& [name, count] : NameCounts())
			names.push_back(name);

		return names;
	};

	unsigned long long DialogueBindings::NamesVersion()
	{
		return names_version;
	};

	#pragma endregion
};

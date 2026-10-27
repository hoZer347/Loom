#pragma once

#include "Loom API.h"
#include "Macro Helpers.h"

#include "DialogueArgs.h"

#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>


namespace Loom
{
	struct LoomObject;

	#pragma region Registration

	/// Every binding, by the object that declared it, and every name one
	/// is declared under, for the editor to offer.
	struct LOOM_API DialogueBindings final
	{
		/// Reads a script's arguments and runs the binding with them, or is false when
		/// they do not read as the ones it takes.
		using Invoke = std::function<bool(const std::vector<std::string>&)>;

		struct Form
		{
			std::string name{ };
			const Invoke* invoke = nullptr;
		};

		/// Hands a binding to the LoomObject under construction on this thread, as a
		/// Serial is, and returns that object.
		static LoomObject* Register(const char* name, const Invoke* invoke);

		static void Unregister(const LoomObject* owner, const Invoke* invoke);

		/// The bindings owner declared, in the order it declared them.
		static std::vector<Form> Of(const LoomObject* owner);

		/// Counted, as each binding type a library holds adds its name once when the
		/// library loads and takes it away when it unloads.
		static void AddName(const char* name);
		static void RemoveName(const char* name);

		/// Every name declared in a library loaded now, sorted.
		static std::vector<std::string> Names();

		/// Goes up each time Names changes.
		static unsigned long long NamesVersion();

		/// Reads tokens as _Args and calls function with them, or is false when they do
		/// not read as those types. A script may say more than a binding takes, but not
		/// less: default arguments are not visible from here.
		template <typename... _Args, typename _Function>
		static bool Call(const std::vector<std::string>& tokens, const _Function& function)
		{
			if (tokens.size() < sizeof...(_Args))
				return false;

			return CallWith<_Args...>(tokens, function, std::index_sequence_for<_Args...>{ });
		};

	private:
		template <typename... _Args, typename _Function, size_t... _Index>
		static bool CallWith(const std::vector<std::string>& tokens, const _Function& function, std::index_sequence<_Index...>)
		{
			std::tuple<std::decay_t<_Args>...> args{ };

			if (!(DialogueArgs::TryCoerce(tokens[_Index], std::get<_Index>(args)) && ...))
				return false;

			function(std::get<_Index>(args)...);

			return true;
		};
	};

	/// A binding type's name, held while the library that has the type is loaded.
	struct DialogueBindingName final
	{
		explicit DialogueBindingName(const char* name) : m_name(name) { DialogueBindings::AddName(m_name); };
		~DialogueBindingName() { DialogueBindings::RemoveName(m_name); };

		DialogueBindingName(const DialogueBindingName&) = delete;
		DialogueBindingName& operator=(const DialogueBindingName&) = delete;

	private:
		const char* m_name;
	};

	/**
	* Loom::DialogueBindingSlot
	* - What DialogueBinding and DialogueMethod share: the invoke registered under name
	*   while it lives, by the object being built around it
	*/
	template <Str name>
	struct DialogueBindingSlot
	{
		// Registered by its address, which is what a copy would not share.
		DialogueBindingSlot(const DialogueBindingSlot&) = delete;
		DialogueBindingSlot& operator=(const DialogueBindingSlot&) = delete;

	protected:
		explicit DialogueBindingSlot(DialogueBindings::Invoke invoke) : m_invoke(std::move(invoke))
		{
			// Using it is what has the library that holds this type name it.
			static_cast<void>(&s_name);

			m_owner = DialogueBindings::Register(name.value, &m_invoke);
		};

		~DialogueBindingSlot() { DialogueBindings::Unregister(m_owner, &m_invoke); };

	private:
		static inline const DialogueBindingName s_name{ name.value };

		const DialogueBindings::Invoke m_invoke;
		LoomObject* m_owner = nullptr;
	};

	#pragma endregion

	/**
	* Loom::DialogueBinding
	* - A command a .dialogue script can run, declared as a member the way a Serial is:
	*   DialogueBinding<"Shake", float> shake = [this](float strength) { ... };
	*   A script's [Shake, 0.5] runs it with 0.5 once a DialogueManager reaches it
	* - LOOM_DIALOGUE_BINDING declares one from a member function instead
	* - Found by a DialogueManager on the components of its own GameObject and then of
	*   each parent above it, after the bindings it was given with Bind
	* - The arguments are read from the script as the types listed, and a command whose
	*   arguments do not read as them is a miss, so declaring the name again with other
	*   types overloads it, the forms tried in the order they were declared
	* - Held and called like the std::function it wraps; one holding nothing is a miss
	* - Its name is offered in the Dialogues panel while the library declaring it is loaded
	* - A member of the LoomObject being built around it, as a Serial is, and only that
	*/
	template <Str name, typename... _Args>
	struct DialogueBinding final : DialogueBindingSlot<name>
	{
		using Function = std::function<void(_Args...)>;

		#pragma region Construction

		DialogueBinding() : DialogueBindingSlot<name>(Bind(this)) { };

		template <typename _Callable>
			requires (
				!std::is_same_v<std::remove_cvref_t<_Callable>, DialogueBinding> &&
				std::is_constructible_v<Function, _Callable&&>)
		DialogueBinding(_Callable&& callable) : DialogueBindingSlot<name>(Bind(this)), m_call(std::forward<_Callable>(callable)) { };

		#pragma endregion

		#pragma region Function

		template <typename _Callable>
			requires (
				!std::is_same_v<std::remove_cvref_t<_Callable>, DialogueBinding> &&
				std::is_assignable_v<Function&, _Callable&&>)
		DialogueBinding& operator=(_Callable&& callable)
		{
			m_call = std::forward<_Callable>(callable);
			return *this;
		};

		void operator()(_Args... args) const
		{
			if (m_call)
				m_call(std::forward<_Args>(args)...);
		};

		explicit operator bool() const { return static_cast<bool>(m_call); };

		#pragma endregion

	private:
		#pragma region Invoking

		static DialogueBindings::Invoke Bind(const DialogueBinding* self)
		{
			return [self](const std::vector<std::string>& tokens) { return self->m_call && DialogueBindings::Call<_Args...>(tokens, self->m_call); };
		};

		#pragma endregion

		#pragma region State

		Function m_call{ };

		#pragma endregion
	};

	/**
	* Loom::DialogueMethod
	* - The binding LOOM_DIALOGUE_BINDING declares for the member function written after
	*   it, which a script's [name, ...] calls with its arguments read as the types that
	*   function takes
	*/
	template <Str name>
	struct DialogueMethod final : DialogueBindingSlot<name>
	{
		template <typename _Self, typename _Class, typename _Return, typename... _Args>
		DialogueMethod(_Self* self, _Return (_Class::*method)(_Args...)) : DialogueBindingSlot<name>(Bind<_Args...>(self, method)) { };

		template <typename _Self, typename _Class, typename _Return, typename... _Args>
		DialogueMethod(const _Self* self, _Return (_Class::*method)(_Args...) const) : DialogueBindingSlot<name>(Bind<_Args...>(self, method)) { };

	private:
		template <typename... _Args, typename _Self, typename _Method>
		static DialogueBindings::Invoke Bind(_Self* self, _Method method)
		{
			return [self, method](const std::vector<std::string>& tokens)
			{
				return DialogueBindings::Call<_Args...>(tokens, [&](auto&... args) { (self->*method)(args...); });
			};
		};
	};
};

/// Makes the member function written after it a command its class's .dialogue scripts
/// can run, under the function's own name, as an attribute would in C#:
///
///		LOOM_DIALOGUE_BINDING(Shout)(const std::string& word) { Append(word + '!'); };
///
/// A script's [Shout, hey] then calls Shout("hey"). A name has one function this way;
/// one with several forms declares each as a Loom::DialogueBinding.
#define LOOM_DIALOGUE_BINDING(name) \
	::Loom::DialogueMethod<#name> loomDialogueBinding##name{ this, &std::remove_pointer_t<decltype(this)>::name }; \
	void name

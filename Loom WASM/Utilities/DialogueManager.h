#pragma once

#include "DialogueArgs.h"
#include "StateMachine.h"

#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace detail
	{
		/// What a callable takes, so a binding can be written as an ordinary lambda and
		/// still be fed from a script's comma-separated text.
		///
		/// The signature is read off the callable itself, so the compiler decides what
		/// each comma-separated script argument is coerced to.
		template <typename T>
		struct function_traits : function_traits<decltype(&std::decay_t<T>::operator())> { };

		template <typename R, typename C, typename... A>
		struct function_traits<R(C::*)(A...) const> { using args = std::tuple<std::decay_t<A>...>; };

		template <typename R, typename C, typename... A>
		struct function_traits<R(C::*)(A...)> { using args = std::tuple<std::decay_t<A>...>; };

		template <typename R, typename... A>
		struct function_traits<R(*)(A...)> { using args = std::tuple<std::decay_t<A>...>; };

		template <typename R, typename... A>
		struct function_traits<R(A...)> { using args = std::tuple<std::decay_t<A>...>; };
	};

	/// Streams a script into a text box, a character at a time, running the commands
	/// written into it as it goes.
	///
	/// A script is plain text with two kinds of token in it:
	///
	///		[Binding, arg, arg]     runs a bound function
	///		&lt;anything&gt;             passed through untouched, for the box's own markup
	///
	/// Everything else streams out one character at a time at the current text speed.
	///
	/// The manager owns the text and announces every change rather than drawing it: a
	/// host draws Text() however it likes and plays Speak's blips however it likes,
	/// Loom having neither a text renderer nor audio. Everything above that -- the
	/// tokenising, the
	/// bindings, the states, the timing -- is unchanged.
	struct DialogueManager : StateMachine<DialogueManager>
	{
		DialogueManager();

		#pragma region Script

		/// The script this manager streams. Set it before Begin.
		void SetScript(std::string script);

		const std::string& Script() const { return _script; };

		#pragma endregion

		#pragma region Text

		/// What the box currently reads. A host draws this.
		const std::string& Text() const { return _text; };

		/// The prompt shown while a line waits on input. Empty unless a script sets it.
		const std::string& ContinueText() const { return _continueText; };

		void SetContinueText(std::string text);

		/// Raised whenever the text changes -- a character streamed, a page cleared. A
		/// host redraws from here rather than polling every frame.
		std::function<void(const std::string& text)> onTextChanged{ };

		/// Raised for every character that streams out, with the level and pitch its blip
		/// would play at. The host plays it; the manager only announces. Pitch is rolled
		/// fresh per blip, which is what makes the voice babble rather than beep.
		std::function<void(char c, float level, float pitch)> onSpeak{ };

		#pragma endregion

		#pragma region Voice

		float voiceVolume = 0.5f;

		/// Random pitch range for each blip -- the wider the gap, the more babble-like.
		float voicePitchMin = 0.85f;
		float voicePitchMax = 1.25f;

		/// Play a blip every Nth visible character (1 = every character). Raise it to thin
		/// out fast text.
		int voiceEvery = 2;

		/// The dial a game's own effects volume reaches the blip through. Nothing in this
		/// library knows what a settings screen is, so the level is handed in rather than
		/// read; unset means full.
		static inline std::function<float()> VoiceLevel{ };

		/// The level the next blip would play at.
		float VoiceOut() const;

		/// Blips one character, if it is one worth blipping.
		void Speak(char c);

		#pragma endregion

		#pragma region Speed

		/// Seconds per character.
		float startingTextSpeed = 0.03f;

		float TextSpeed() const { return _textSpeed; };

		#pragma endregion

		#pragma region Running

		/// Builds the bindings if needed, fills the queue from the script, and starts.
		/// Re-fills the queue every call, so the full text streams however the last run
		/// left it.
		void Begin();

		/// Streams one arbitrary line rather than the assigned script; same parse, same
		/// bindings.
		void Begin(const std::string& text);

		/// False until the manager has what it needs to run -- a script, at minimum.
		bool IsReady() const { return _ready; };

		#pragma endregion

		#pragma region Bindings

		/// Binds a name a script can call. The callable's own signature says what its
		/// arguments are, and a token that will not read as one of them is a miss:
		///
		///		Bind("Pause", [this]() { PushFirst&lt;St_Dg_WaitForInput&gt;(); });
		///		Bind("TextSpeed", [this](float speed) { _textSpeed = speed; });
		///
		/// A name may be bound more than once, and the forms are tried in the order they
		/// were bound. Keying by name alone would let a second binding silently replace
		/// the first, so [TextSpeed, 0.1] would fall through to the string form and
		/// report the warning meant for a misspelt word.
		template <typename _Callable>
		void Bind(const std::string& name, _Callable&& callable)
		{
			using Args = typename detail::function_traits<_Callable>::args;

			_bindings[Lowered(name)].push_back(
				[callable = std::forward<_Callable>(callable)](const std::vector<std::string>& tokens) -> bool
				{
					return Invoke(callable, tokens, static_cast<Args*>(nullptr));
				});
		};

		/// Drops every form bound under that name.
		void Unbind(const std::string& name);

		#pragma endregion

		#pragma region Parsing

		/// Splits a script into [Binding] commands, &lt;markup&gt; and the plain text
		/// between, dropping the whitespace a cleared page would otherwise open with.
		static std::vector<std::string> Tokens(const std::string& text);

		static bool IsBinding(const std::string& token);
		static bool IsRichText(const std::string& token);

		#pragma endregion

		std::string MachineName() const override { return "DialogueManager"; };

	protected:
		friend struct St_Dg_StreamPlainText;
		friend struct St_Dg_StreamRichText;
		friend struct St_Dg_DialogueEnd;

		void OnStart() override;

		/// Appends to the box, and tells whoever is drawing it.
		void Append(const std::string& text);
		void Append(char c);

		/// Empties the box.
		void ClearText();

	private:
		#pragma region Binding plumbing

		template <typename _Callable, typename... _Args>
		static bool Invoke(const _Callable& callable, const std::vector<std::string>& tokens, std::tuple<_Args...>*)
		{
			// A form that wants more arguments than the script gave it is not the form
			// the script meant. Default arguments are not visible from here, so a shorter
			// form has to be bound separately.
			if (tokens.size() < sizeof...(_Args))
				return false;

			return InvokeWith<_Callable, _Args...>(callable, tokens, std::index_sequence_for<_Args...>{ });
		};

		template <typename _Callable, typename... _Args, size_t... _Index>
		static bool InvokeWith(const _Callable& callable, const std::vector<std::string>& tokens, std::index_sequence<_Index...>)
		{
			std::tuple<_Args...> args{ };

			// Every argument has to read before any of them is used -- a half-coerced call
			// would run the binding with a default in place of an argument the script
			// plainly wrote.
			const bool coerced =
				(DialogueArgs::TryCoerce(tokens[_Index], std::get<_Index>(args)) && ...);

			if (!coerced)
				return false;

			callable(std::get<_Index>(args)...);

			return true;
		};

		static std::string Lowered(const std::string& s);

		static bool IsClearBinding(const std::string& token);

		/// content is a binding token without its brackets: "Name, Arg1, Arg2".
		void InvokeBinding(const std::string& content);

		#pragma endregion

		/// No-ops once ready, so Begin may call it after OnStart already has.
		void Build();

		void QueueScript();
		void Queue(const std::string& text);

		std::string _script{ };
		std::string _text{ };
		std::string _continueText{ };

		float _textSpeed = 0.03f;

		bool _ready = false;

		// OnStart may run a frame later than a manual Begin; this stops it re-queueing
		// over one.
		bool _begun = false;

		int _voiceCharCount = 0;

		std::unordered_map<
			std::string,
			std::vector<std::function<bool(const std::vector<std::string>&)>>> _bindings{ };
	};
};

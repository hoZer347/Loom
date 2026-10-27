#pragma once

#include "Asset.h"
#include "DialogueArgs.h"
#include "DialogueBinding.h"
#include "RichText.h"
#include "Serial.h"
#include "StateMachine.h"

#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>


namespace Loom
{
	struct UiText;

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

	/// What every DialogueManager does, written once: streams a script into a text box,
	/// a character at a time, running the commands written into it as it goes.
	///
	/// A script is plain text with commands in brackets in it, [Binding, arg, arg], each
	/// a DialogueBinding or a Bind. Its own are Proceed, Clear, Pause and TextSpeed, and
	/// Color and Size with their closing /Color and /Size, which write the RichText tag
	/// into the text for the box to draw. Everything else streams out one character at
	/// a time at the current text speed.
	///
	/// The script is the .dialogue file DialogueFile names, unless SetScript gives it
	/// one. The manager owns the text and announces every change through onTextChanged;
	/// with none set, it writes the text into the UiText on its GameObject or the first
	/// one under it. Speak's blips go to onSpeak for a host to play however it likes.
	struct LOOM_API DialogueManagerBase : StateMachineBase
	{
		DialogueManagerBase();

		static constexpr char extension[] = ".dialogue";

		#pragma region Running

		/// The file streamed when no script has been set.
		virtual std::string DialogueFile() const { return ""; };

		/// The script this manager streams. Set it before Begin.
		void SetScript(std::string script);

		const std::string& Script() const { return _script; };

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

		/// Seconds per character.
		float startingTextSpeed = 0.03f;

		float TextSpeed() const { return _textSpeed; };

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

			Binding& binding = _bindings[Lowered(name)];

			if (binding.name.empty())
				binding.name = name;

			binding.forms.push_back(
				[callable = std::forward<_Callable>(callable)](const std::vector<std::string>& tokens) -> bool
				{
					return Invoke(callable, tokens, static_cast<Args*>(nullptr));
				});
		};

		/// Drops every form bound under that name.
		void Unbind(const std::string& name);

		/// Every name bound, as it was first bound, and every DialogueBinding's that
		/// Declared finds.
		std::vector<std::string> Commands() const;

		/// What the inspector's New Binding runs, with the name of the component type it
		/// was pressed on. The editor sets it, and without it the button is disabled.
		static std::function<void(const std::string& type)> onNewBinding;

		#pragma endregion

		#pragma region Parsing

		/// Splits a script into its [bracketed] commands and the plain text between,
		/// dropping the whitespace a cleared page would otherwise open with.
		static std::vector<std::string> Tokens(const std::string& text);

		static bool IsBinding(const std::string& token);
		static bool IsRichText(const std::string& token);

		#pragma endregion

	protected:
		friend struct St_Dg_StreamPlainText;
		friend struct St_Dg_DialogueEnd;

		#pragma region Running

		void OnStart() override;

		#pragma endregion

		#pragma region Text

		/// Appends to the box, and tells whoever is drawing it.
		void Append(const std::string& text);
		void Append(char c);

		/// Empties the box.
		void ClearText();

		/// Hands the text to onTextChanged, or to the UiText that shows it.
		void Show();

		/// The UiText on this GameObject, or the first one under it that is enabled.
		UiText* Box() const;

		/// Appends a RichText tag, [name, arg] or [name] when arg is empty.
		void AppendTag(std::string_view name, const std::string& arg = "");

		#pragma endregion

		#pragma region Gui

		/// The New Binding button, which hands this component's type to onNewBinding.
		void DrawNewBinding();

		#pragma endregion

	private:
		#pragma region Bindings

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

		/// The GameObject the manager is on, or none.
		GameObject* Object() const;

		/// The manager's own DialogueBinding members, then those of the other components
		/// on its GameObject, then of each parent above it.
		std::vector<DialogueBindings::Form> Declared() const;

		/// content is a binding token without its brackets: "Name, Arg1, Arg2". Bind's
		/// forms are tried before Declared's.
		void InvokeBinding(const std::string& content);

		#pragma endregion

		#pragma region Parsing

		static bool IsClearBinding(const std::string& token);

		#pragma endregion

		#pragma region Running

		/// No-ops once ready, so Begin may call it after OnStart already has.
		void Build();

		void QueueScript();
		void Queue(const std::string& text);

		#pragma endregion

		std::string _script{ };
		std::string _text{ };
		std::string _continueText{ };

		float _textSpeed = 0.03f;

		bool _ready = false;

		// OnStart may run a frame later than a manual Begin; this stops it re-queueing
		// over one.
		bool _begun = false;

		int _voiceCharCount = 0;

		struct Binding
		{
			std::string name{ };
			std::vector<std::function<bool(const std::vector<std::string>&)>> forms{ };
		};

		std::unordered_map<std::string, Binding> _bindings{ };

		#pragma region Default Bindings

		DialogueBinding<"Proceed"> _proceed = [this]() { Proceed(); };
		DialogueBinding<"Clear"> _clear = [this]() { ClearText(); };
		DialogueBinding<"Pause"> _pause = [this]() { WaitForInput(); };
		DialogueBinding<"TextSpeed", float> _textSpeedBy = [this](float speed) { _textSpeed = speed; };
		DialogueBinding<"TextSpeed", std::string> _textSpeedNamed = [this](const std::string& speed) { SetTextSpeed(speed); };

		// Named as RichText::ColorTag and SizeTag are.
		DialogueBinding<"Color", std::string> _color = [this](const std::string& color) { AppendTag(RichText::ColorTag, color); };
		DialogueBinding<"/Color"> _colorEnd = [this]() { AppendTag(std::string(1, RichText::Close) + std::string(RichText::ColorTag)); };
		DialogueBinding<"Size", float> _size = [this](float size) { AppendTag(RichText::SizeTag, std::format("{}", size)); };
		DialogueBinding<"/Size"> _sizeEnd = [this]() { AppendTag(std::string(1, RichText::Close) + std::string(RichText::SizeTag)); };

		void WaitForInput();
		void SetTextSpeed(const std::string& speed);

		#pragma endregion
	};

	/// A component streaming a .dialogue file, whose commands are its DialogueBinding
	/// members and those of the components around it. Self-typed as a StateMachine is:
	///
	///		struct Narrator : Loom::DialogueManager&lt;Narrator&gt;
	///		{
	///			Loom::DialogueBinding&lt;"Shake", float&gt; shake = [this](float strength) { };
	///		};
	///
	/// A project's are filed under Custom/Dialogues in Add Component.
	template <typename _Self, Str _Category = "Custom/Dialogues">
	struct DialogueManager : StateMachine<_Self, _Category, DialogueManagerBase>
	{
		/// The file streamed when no script has been set.
		Serial<Asset<DialogueManagerBase::extension>> dialogue;

		std::string DialogueFile() const override { return *dialogue; };

		void OnGui() override { this->DrawNewBinding(); };
	};

	namespace Basic
	{
		/// A manager with no bindings of its own, which the Cutscene menu's Dialogue Box
		/// puts on. Registered as DialogueManager, as Basic::StateMachine is as
		/// StateMachine, and has no header of its own to add a binding to.
		struct DialogueManager final : Loom::DialogueManager<DialogueManager, "Logic">
		{
			void OnGui() override { };
		};
	};
};

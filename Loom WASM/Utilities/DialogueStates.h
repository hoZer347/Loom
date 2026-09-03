#pragma once

#include "DialogueInput.h"
#include "DialogueManager.h"
#include "Duration.h"
#include "State.h"

#include <functional>
#include <string>


namespace Loom
{
	/// Opens a run. Nothing but a hand-off, so a script always has a state to proceed out
	/// of rather than starting halfway through its first line.
	struct St_Dg_Begin final : StateOf<DialogueManager>
	{
		std::string Name() const override { return "St_Dg_Begin"; };

		void OnUpdate() override { Proceed(); };
	};

	/// Closes a run, emptying the box.
	struct St_Dg_DialogueEnd final : StateOf<DialogueManager>
	{
		std::string Name() const override { return "St_Dg_DialogueEnd"; };

		void OnEnter(State* lastState) override;
	};

	/// Runs one [Binding] token and moves on.
	struct St_Dg_InvokeBinding final : StateOf<DialogueManager>
	{
		std::function<void()> binding{ };

		std::string Name() const override { return "St_Dg_InvokeBinding"; };

		std::string Describe() const override { return "binding"; };

		void OnUpdate() override
		{
			if (binding)
				binding();

			Proceed();
		};
	};

	/// Types one run of plain text out, a character at a time.
	struct St_Dg_StreamPlainText final : StateOf<DialogueManager>
	{
		std::string text{ };

		std::string Name() const override { return "St_Dg_StreamPlainText"; };

		std::string Describe() const override { return text; };

		void OnEnter(State* lastState) override;

		void OnUpdate() override;

	private:
		Duration duration{ };
		size_t amountStreamed = 0;
	};

	/// Passes one markup token straight through -- it is an instruction to the box, not
	/// something to type out.
	struct St_Dg_StreamRichText final : StateOf<DialogueManager>
	{
		std::string text{ };

		std::string Name() const override { return "St_Dg_StreamRichText"; };

		std::string Describe() const override { return text; };

		void OnUpdate() override;
	};

	/// Holds a finished line on screen until the player says carry on.
	struct St_Dg_WaitForInput final : StateOf<DialogueManager>
	{
		std::string Name() const override { return "St_Dg_WaitForInput"; };

		void OnUpdate() override
		{
			// A left click (how a cutscene is usually driven), the keyboard, or any of a
			// pad's four face buttons -- see DialogueInput, which owns that list so this
			// state and the streaming one cannot disagree about what "carry on" is. The
			// mouse is asked here rather than there because only this state takes a click:
			// a click during the stream is how a box gets dragged around.
			if (DialogueInput::Clicked() || DialogueInput::ContinuedAnyhow())
				Proceed();
		};
	};
};

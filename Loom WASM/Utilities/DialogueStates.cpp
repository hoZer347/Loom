#include "DialogueStates.h"


namespace Loom
{
	void St_Dg_DialogueEnd::OnEnter(StateBase* lastState)
	{
		Focus()->ClearText();
	};

	void St_Dg_StreamPlainText::OnEnter(StateBase* lastState)
	{
		duration.Reset(Focus()->TextSpeed());
	};

	void St_Dg_StreamPlainText::OnUpdate()
	{
		if (duration.Tick() && amountStreamed < text.size())
		{
			duration.Reset(Focus()->TextSpeed());

			const char c = text[amountStreamed++];

			Focus()->Append(c);
			Focus()->Speak(c);
		};

		// The same press skips the stream or advances, or taps while typing look dropped.
		if (DialogueInput::ContinuedAnyhow())
		{
			Focus()->Append(text.substr(amountStreamed));

			Proceed();

			return;
		};

		if (amountStreamed >= text.size())
			Proceed();
	};
};

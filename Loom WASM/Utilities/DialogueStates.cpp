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

		// A face button or a key skips the stream as well as advancing a finished line --
		// the same press has to mean "carry on" at both ends of a line, or the box
		// swallows every tap made while it is still typing and reads as dropped input.
		if (DialogueInput::ContinuedAnyhow())
		{
			Focus()->Append(text.substr(amountStreamed));

			Proceed();

			return;
		};

		if (amountStreamed >= text.size())
			Proceed();
	};

	void St_Dg_StreamRichText::OnUpdate()
	{
		Focus()->Append(text);

		Proceed();
	};
};

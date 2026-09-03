#pragma once

#include "Loom.h"


namespace SpaceExplorers
{
	struct Ship : Loom::Component<Ship>
	{
		Ship();
		virtual ~Ship();

	private:
		Loom::Mesh* mesh = nullptr;
		Loom::Material* material = nullptr;
	};
};

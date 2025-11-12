module;

#include "Component.h"

export module Demos;

import <initializer_list>;
import <string>;


namespace Loom
{
	export struct DemosWindow :
		Component<DemosWindow>
	{
		void OnGui() override;
		void OnAttach() override;
	};

	export void Fractal();
};

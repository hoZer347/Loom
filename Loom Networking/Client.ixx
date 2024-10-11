export module Client;

import Component;


namespace Loom
{
	export struct Client :
		public Component<Client>
	{
		void OnAttach() override;
		void OnUpdate() override;
		void OnDetach() override;
	};
};

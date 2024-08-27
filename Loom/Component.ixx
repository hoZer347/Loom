export module Component;

namespace Loom
{
	export struct Component
	{
		virtual void OnAttach() { };
		virtual void OnDetach() { };
		virtual void OnGui()	{ };
		virtual void Update()	{ };
	};
};

export module Buffer;

import Component;


namespace Loom
{
	export struct FrameBuffer;
	export struct Engine;

	export struct Buffer final :
		public Component<Buffer>
	{
		Buffer(Engine* engine);

		~Buffer();

		void OnRender() override;
		void OnGui() override;

		FrameBuffer* frameBuffer;
		Engine* engine;

		void* data;

		int w, h;
	};
};

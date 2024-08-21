#pragma once

namespace Loom
{
	class Renderer;

	class Application final
	{
		Application();
		~Application();


	private:
		Renderer* renderer = nullptr;
	};
};

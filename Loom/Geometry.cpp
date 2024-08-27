import Geometry;

import Engine;

#include "Geometry.cuh"
#include "Buffer.cuh"

#include <iostream>


namespace Loom
{
	Rect::Rect(
		const unsigned int& x,
		const unsigned int& y,
		const unsigned int& w,
		const unsigned int& h)
	{
		dimensions = (unsigned int*)malloc(sizeof(unsigned int) * 2);
		dimensions[0] = w;
		dimensions[1] = h;

		const unsigned int position[2]{ x, y };
		BufferMalloc(this->position, sizeof(unsigned int) * 2);
		BufferSend(this->position, position, sizeof(unsigned int) * 2);
	};

	Rect::~Rect()
	{
		BufferFree((void*&)position);
		BufferFree((void*&)dimensions);
	};

	void Rect::Update()
	{
		DrawRect(Engine::GPU_buffer, position, dimensions, Engine::GPU_dims);
	};
};

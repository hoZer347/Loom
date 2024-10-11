#include "Geometry.cuh"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <iostream>


namespace Loom
{
	__global__ void __DrawRect(
		unsigned char* buffer_data,
		const unsigned int buffer_w,
		const unsigned int buffer_h,
		const unsigned int rect_x,
		const unsigned int rect_y,
		const unsigned int rect_w,
		const unsigned int rect_h)
	{
		// Calculate the global thread index
		unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
		unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;

		// Ensure the thread is within the buffer bounds
		if (x >= buffer_w || y >= buffer_h) return;

		// Check if the pixel is inside the rectangle
		if (x >= rect_x &&
			x < rect_x + rect_w &&
			y >= rect_y &&
			y < rect_y + rect_h)
		{
			unsigned int index = (y * buffer_w + x) * 4; // Assuming 4 bytes per pixel (RGBA)

			// Set the pixel color
			buffer_data[index]		= 128;
			buffer_data[index + 1]	= 0;
			buffer_data[index + 2]	= 0;
			buffer_data[index + 3]	= 255;
		};
	};

	void DrawRectToBuffer(
		void* buffer_data,
		const unsigned int& buffer_w,
		const unsigned int& buffer_h,
		const unsigned int& rect_x,
		const unsigned int& rect_y,
		const unsigned int& rect_w,
		const unsigned int& rect_h)
	{
		dim3 blockSize(16, 16); // 16x16 threads per block
		dim3 gridSize(	(buffer_w + blockSize.x - 1) / blockSize.x,
						(buffer_h + blockSize.y - 1) / blockSize.y);

		__DrawRect<<<gridSize, blockSize>>>(
			(unsigned char*)buffer_data,
			buffer_w,
			buffer_h,
			rect_x,
			rect_y,
			rect_w,
			rect_h);
	};
};

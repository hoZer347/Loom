#include "Buffer.cuh"

#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <iostream>


namespace Loom
{
	__device__ unsigned char* buffer;

	__global__ static void __DrawRect(
		void* output,
		const unsigned int* position,
		const unsigned int* buffer_dims)
	{
		((unsigned char*)output)[threadIdx.x + 0] = 255;
		((unsigned char*)output)[threadIdx.x + 1] = 0;
		((unsigned char*)output)[threadIdx.x + 2] = 0;
		((unsigned char*)output)[threadIdx.x + 3] = 255;
	};

	void DrawRect(
		void* output,
		const unsigned int* position,
		const unsigned int* dimensions,
		const unsigned int* buffer_dims)
	{
		__DrawRect<<<{ dimensions[0], dimensions[1] }, 1>>>(
			output,
			position,
			buffer_dims);
	};

	void BufferSet(unsigned char* buffer_ptr)
	{
		buffer = buffer_ptr;
	};

	void BufferMalloc(void* ptr, size_t size) noexcept
	{
		if (cudaDeviceSynchronize())
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;

		if (cudaMalloc(&ptr, size))
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;
	};

	void BufferSend(void* dst, const void* src, size_t size) noexcept
	{
		if (cudaDeviceSynchronize())
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;

		if (cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice))
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;
	};

	void BufferCopy(void* dst, const void* src, size_t size) noexcept
	{
		if (cudaDeviceSynchronize())
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;

		if (cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost))
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;
	};

	void BufferFree(void* ptr) noexcept
	{
		if (cudaDeviceSynchronize())
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;

		if (cudaFree(ptr))
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;
	};

	void BufferSync() noexcept
	{
		if (cudaDeviceSynchronize())
			std::cout << cudaGetErrorString(cudaGetLastError()) << std::endl;
	};
};

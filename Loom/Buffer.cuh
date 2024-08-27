namespace Loom
{
	void DrawRect(
		void* output,
		const unsigned int* position,
		const unsigned int* dimensions,
		const unsigned int* buffer_dims);

	void BufferSet		(unsigned char* buffer_ptr) noexcept;

	void BufferMalloc	(void* ptr, size_t size) noexcept;

	void BufferSend		(void* dst, const void* src, size_t size) noexcept;

	void BufferCopy		(void* dst, const void* src, size_t size) noexcept;

	void BufferFree		(void* ptr) noexcept;

	void BufferSync		() noexcept;
};

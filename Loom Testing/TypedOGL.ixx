module;

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <array>
#include <span>
#include <bit>

#include <concepts>

export module Loom.TypedOGL;

namespace Loom
{
	namespace Device
	{
		export enum BufferID : GLuint {};
		export enum ArrayBufferID : GLuint {};
		export enum CopyReadBufferID : GLuint {};
		export enum CopyWriteBufferID : GLuint {};
		export enum ElementArrayBufferID : GLuint {};
		export enum PixelPackBufferID : GLuint {};
		export enum PixelUnpackBufferID : GLuint {};
		export enum TextureBufferID : GLuint {};
		export enum TransformFeedbackBufferID : GLuint {};
		export enum UniformBufferID : GLuint {};

		template<class Ty>
		concept IsTypedBuffer = std::same_as<Ty, ArrayBufferID> ||
			std::same_as<Ty, CopyReadBufferID> ||
			std::same_as<Ty, CopyWriteBufferID> ||
			std::same_as<Ty, ElementArrayBufferID> ||
			std::same_as<Ty, PixelPackBufferID> ||
			std::same_as<Ty, PixelUnpackBufferID> ||
			std::same_as<Ty, TextureBufferID> ||
			std::same_as<Ty, TransformFeedbackBufferID> ||
			std::same_as<Ty, UniformBufferID>;

		template<class Ty>
		concept IsBufferType = std::same_as<Ty, BufferID> || IsTypedBuffer<Ty>;
;

		template<IsBufferType Ty>
		constexpr GLenum bufferTypeToTarget = 0;

		template<>
		constexpr GLenum bufferTypeToTarget<ArrayBufferID> = GL_ARRAY_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<CopyReadBufferID> = GL_COPY_READ_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<CopyWriteBufferID> = GL_COPY_WRITE_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<ElementArrayBufferID> = GL_ELEMENT_ARRAY_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<PixelPackBufferID> = GL_PIXEL_PACK_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<PixelUnpackBufferID> = GL_PIXEL_UNPACK_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<TextureBufferID> = GL_TEXTURE_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<TransformFeedbackBufferID> = GL_TRANSFORM_FEEDBACK_BUFFER;

		template<>
		constexpr GLenum bufferTypeToTarget<UniformBufferID> = GL_UNIFORM_BUFFER;

		export enum BufferUsage : GLenum
		{
			Stream_Draw = GL_STREAM_DRAW,
			Stream_Read = GL_STREAM_READ,
			Stream_Copy = GL_STREAM_COPY,
			Static_Draw = GL_STATIC_DRAW,
			Static_Read = GL_STATIC_READ,
			Static_Copy = GL_STATIC_COPY,
			Dynamic_Draw = GL_DYNAMIC_DRAW,
			Dynamic_Read = GL_DYNAMIC_READ,
			Dynamic_Copy = GL_DYNAMIC_COPY
		};

		export BufferID GenBuffer()
		{
			GLuint buffer;
			glGenBuffers(1, &buffer);
			return static_cast<BufferID>(buffer);
		}

		export template<GLsizei N>
		std::array<BufferID, N> GenBuffers()
		{
			std::array<GLuint, N> tempBuffers;
			glGenBuffers(tempBuffers.size(), tempBuffers.data());
			return std::bit_cast<std::array<BufferID, N>>(tempBuffers);
		}

		export template<IsTypedBuffer Ty>
		void BindBuffer(Ty id)
		{
			glBindBuffer(bufferTypeToTarget<Ty>, static_cast<GLuint>(id));
		}

		export template<IsTypedBuffer Ty>
		void RecreateBuffer(Ty id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			BindBuffer(id);
			glBufferData(bufferTypeToTarget<Ty>, size, optionalInitialData, usage);
		}

		template<IsTypedBuffer Ty>
		Ty CreateBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			Ty typedID{ static_cast<GLuint>(id) };
			RecreateBuffer(typedID, size, optionalInitialData, usage);
			return typedID;
		}

		template<IsTypedBuffer Ty>
		void UpdateBuffer(Ty id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			BindBuffer(id);
			glBufferSubData(bufferTypeToTarget<Ty>, offset, size, data);
		}

		export ArrayBufferID CreateArrayBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<ArrayBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		ArrayBufferID CreateArrayBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateArrayBuffer(id, sizeof(Ty), &data, usage);
		}

		export ArrayBufferID CreateArrayBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateArrayBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		ArrayBufferID CreateArrayBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateArrayBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateArrayBuffer(ArrayBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateArrayBuffer(ArrayBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateArrayBuffer(id, offset, sizeof(data), &data);
		}

		export CopyReadBufferID CreateCopyReadBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<CopyReadBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		CopyReadBufferID CreateCopyReadBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateCopyReadBuffer(id, sizeof(Ty), &data, usage);
		}

		export CopyReadBufferID CreateCopyReadBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateCopyReadBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		CopyReadBufferID CreateCopyReadBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateCopyReadBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateCopyReadBuffer(CopyReadBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateCopyReadBuffer(CopyReadBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateCopyReadBuffer(id, offset, sizeof(data), &data);
		}

		export CopyWriteBufferID CreateCopyWriteBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<CopyWriteBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		CopyWriteBufferID CreateCopyWriteBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateCopyWriteBuffer(id, sizeof(Ty), &data, usage);
		}

		export CopyWriteBufferID CreateCopyWriteBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateCopyWriteBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		CopyWriteBufferID CreateCopyWriteBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateCopyWriteBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateCopyWriteBuffer(CopyWriteBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateCopyWriteBuffer(CopyWriteBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateCopyWriteBuffer(id, offset, sizeof(data), &data);
		}

		export ElementArrayBufferID CreateElementArrayBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<ElementArrayBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		ElementArrayBufferID CreateElementArrayBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateElementArrayBuffer(id, sizeof(Ty), &data, usage);
		}

		export ElementArrayBufferID CreateElementArrayBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateElementArrayBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		ElementArrayBufferID CreateElementArrayBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateElementArrayBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateElementArrayBuffer(ElementArrayBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateElementArrayBuffer(ElementArrayBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateElementArrayBuffer(id, offset, sizeof(data), &data);
		}

		export PixelPackBufferID CreatePixelPackBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<PixelPackBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		PixelPackBufferID CreatePixelPackBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreatePixelPackBuffer(id, sizeof(Ty), &data, usage);
		}

		export PixelPackBufferID CreatePixelPackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreatePixelPackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		PixelPackBufferID CreatePixelPackBuffer(const Ty& data, BufferUsage usage)
		{
			return CreatePixelPackBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdatePixelPackBuffer(PixelPackBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdatePixelPackBuffer(PixelPackBufferID id, GLintptr offset, const Ty& data)
		{
			UpdatePixelPackBuffer(id, offset, sizeof(data), &data);
		}

		export PixelUnpackBufferID CreatePixelUnpackBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<PixelUnpackBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		PixelUnpackBufferID CreatePixelUnpackBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreatePixelUnpackBuffer(id, sizeof(Ty), &data, usage);
		}

		export PixelUnpackBufferID CreatePixelUnpackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreatePixelUnpackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		PixelUnpackBufferID CreatePixelUnpackBuffer(const Ty& data, BufferUsage usage)
		{
			return CreatePixelUnpackBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdatePixelUnpackBuffer(PixelUnpackBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdatePixelUnpackBuffer(PixelUnpackBufferID id, GLintptr offset, const Ty& data)
		{
			UpdatePixelUnpackBuffer(id, offset, sizeof(data), &data);
		}

		export TextureBufferID CreateTextureBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<TextureBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		TextureBufferID CreateTextureBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateTextureBuffer(id, sizeof(Ty), &data, usage);
		}

		export TextureBufferID CreateTextureBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateTextureBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		TextureBufferID CreateTextureBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateTextureBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateTextureBuffer(TextureBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateTextureBuffer(TextureBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateTextureBuffer(id, offset, sizeof(data), &data);
		}

		export TransformFeedbackBufferID CreateTransformFeedbackBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<TransformFeedbackBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		TransformFeedbackBufferID CreateTransformFeedbackBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateTransformFeedbackBuffer(id, sizeof(Ty), &data, usage);
		}

		export TransformFeedbackBufferID CreateTransformFeedbackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateTransformFeedbackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		TransformFeedbackBufferID CreateTransformFeedbackBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateTransformFeedbackBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateTransformFeedbackBuffer(TransformFeedbackBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateTransformFeedbackBuffer(TransformFeedbackBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateTransformFeedbackBuffer(id, offset, sizeof(data), &data);
		}

		export UniformBufferID CreateUniformBuffer(BufferID id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<UniformBufferID>(id, size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniformBufferID CreateUniformBuffer(BufferID id, const Ty& data, BufferUsage usage)
		{
			return CreateUniformBuffer(id, sizeof(Ty), &data, usage);
		}

		export UniformBufferID CreateUniformBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateUniformBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniformBufferID CreateUniformBuffer(const Ty& data, BufferUsage usage)
		{
			return CreateUniformBuffer(GenBuffer(), sizeof(Ty), &data, usage);
		}

		export void UpdateUniformBuffer(UniformBufferID id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			UpdateBuffer(id, offset, size, data);
		}

		export template<class Ty>
		void UpdateUniformBuffer(UniformBufferID id, GLintptr offset, const Ty& data)
		{
			UpdateUniformBuffer(id, offset, sizeof(data), &data);
		}

		export template<IsBufferType Ty>
		void DeleteBuffer(Ty buffer)
		{
			GLuint untypedBuffer = static_cast<GLuint>(buffer);
			glDeleteBuffers(1, &untypedBuffer);
		}

		export template<IsBufferType Ty>
		void DeleteBuffers(std::span<const Ty> buffers)
		{
			glDeleteBuffers(buffers.size(), reinterpret_cast<const GLuint*>(buffers.data()));
		}
	}

	namespace Rendering
	{
		export enum ClearFlag : GLbitfield
		{
			Color_Buffer =  GL_COLOR_BUFFER_BIT,
			Depth_Buffer = GL_DEPTH_BUFFER_BIT,
			Stencil_Buffer =  GL_STENCIL_BUFFER_BIT
		};

		constexpr ClearFlag operator|(ClearFlag lh, ClearFlag rh)
		{
			return ClearFlag{ static_cast<GLbitfield>(lh) | static_cast<GLbitfield>(rh) };
		}

		constexpr ClearFlag operator&(ClearFlag lh, ClearFlag rh)
		{
			return ClearFlag{ static_cast<GLbitfield>(lh) & static_cast<GLbitfield>(rh) };
		}

		constexpr ClearFlag operator^(ClearFlag lh, ClearFlag rh)
		{
			return ClearFlag{ static_cast<GLbitfield>(lh) ^ static_cast<GLbitfield>(rh) };
		}

		constexpr ClearFlag operator~(ClearFlag lh)
		{
			return ClearFlag{ ~static_cast<GLbitfield>(lh) };
		}

		export void Clear(ClearFlag flag)
		{
			glClear(static_cast<GLbitfield>(flag));
		}

		export void SetClearColorValue(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
		{
			glClearColor(red, green, blue, alpha);
		}

		export void SetClearDepthValue(GLclampd value)
		{
			glClearDepth(value);
		}

		export void SetClearStencilValue(GLint value)
		{
			glClearStencil(value);
		}

		export enum Feature : GLenum
		{
			Blend = GL_BLEND,
			Clip_Distance0 = GL_CLIP_DISTANCE0,
			Clip_Distance1 = GL_CLIP_DISTANCE1,
			Clip_Distance2 = GL_CLIP_DISTANCE2,
			Clip_Distance3 = GL_CLIP_DISTANCE3,
			Clip_Distance4 = GL_CLIP_DISTANCE4,
			Clip_Distance5 = GL_CLIP_DISTANCE5,
			Color_Logic_Op = GL_COLOR_LOGIC_OP,
			Cull_Face = GL_CULL_FACE,
			Depth_Clamp = GL_DEPTH_CLAMP,
			Depth_Test = GL_DEPTH_TEST,
			Dither = GL_DITHER,
			FrameBuffer_SRGB = GL_FRAMEBUFFER_SRGB,
			Line_Smooth = GL_LINE_SMOOTH,
			Multisample = GL_MULTISAMPLE,
			Polygon_Offset_Line = GL_POLYGON_OFFSET_LINE,
			Polygon_Smooth = GL_POLYGON_SMOOTH,
			Primitive_Restart = GL_PRIMITIVE_RESTART,
			Sample_Alpha_To_Coverage = GL_SAMPLE_ALPHA_TO_COVERAGE,
			Sample_Alpha_To_One = GL_SAMPLE_ALPHA_TO_ONE,
			Sample_Coverage = GL_SAMPLE_COVERAGE,
			Scissor_Test = GL_SCISSOR_TEST,
			Stencil_Test = GL_STENCIL_TEST,
			Texture_Cube_Map_Seamless = GL_TEXTURE_CUBE_MAP_SEAMLESS,
			Program_Point_Size = GL_PROGRAM_POINT_SIZE
		};

		export void Enable(Feature feature)
		{
			glEnable(static_cast<GLenum>(feature));
		}

		export void Enablei(Feature feature, GLuint index)
		{
			glEnablei(static_cast<GLenum>(feature), index);
		}

		export void Enable(std::span<Feature> features)
		{
			for(auto f : features)
			{
				Enable(f);
			}
		}

		export template<size_t Size>
		void Enable(std::array<Feature, Size> features)
		{
			Enable(std::span{ features });
		}

		export void Disable(Feature feature)
		{
			glDisable(static_cast<GLenum>(feature));
		}

		export void Disablei(Feature feature, GLuint index)
		{
			glDisablei(static_cast<GLenum>(feature), index);
		}

		export void Disable(std::span<Feature> features)
		{
			for(auto f : features)
			{
				Disable(f);
			}
		}

		export template<size_t Size>
		void Disable(std::array<Feature, Size> features)
		{
			Disable(std::span{ features });
		}

		export auto IsEnabled(Feature feature)
		{
			return glIsEnabled(static_cast<GLenum>(feature));
		}

		export auto IsEnabledi(Feature feature, GLuint index)
		{
			return glIsEnabledi(static_cast<GLenum>(feature), index);
		}
	}

}
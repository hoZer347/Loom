module;

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <array>
#include <span>
#include <bit>
#include <string_view>

#include <concepts>
#include <utility>

export module Loom.TypedOGL;

namespace Loom
{
	namespace Device
	{
		export template<class Ty>
		struct DefaultDeleter;

		export template<class Ty, class Deleter = DefaultDeleter<Ty>>
		class UniqueID
		{
		private:
			Ty id = Ty{ 0 };
			[[msvc::no_unique_address]] Deleter deleter = Deleter{};
		public:
			UniqueID() = default;
			UniqueID(std::nullptr_t) {}
			UniqueID(Ty id) :
				id{ id }
			{

			}

			UniqueID(Ty id, Deleter deleter) :
				id{ id },
				deleter{ std::move(deleter) }
			{

			}
			UniqueID(const UniqueID&) = delete;
			UniqueID(UniqueID&& other) noexcept :
				id{ std::exchange(other.id, Ty{ 0 }) }
			{

			}

			UniqueID& operator=(std::nullptr_t) noexcept
			{
				UniqueID temp;
				swap(temp);
				return *this;
			}
			UniqueID& operator=(const UniqueID&) = delete;
			UniqueID& operator=(UniqueID&& other) noexcept
			{
				id = std::exchange(other.id, Ty{ 0 });
				return *this;
			}

			~UniqueID()
			{
				deleter(id);
			}

			void swap(UniqueID& other) noexcept
			{
				std::swap(id, other.id);
			}

		public:
			Ty release() { return std::exchange(id, Ty{ 0 }); }
			Ty get() const { return id; }
		};

		export enum class BufferID : GLuint {};
		export enum class ArrayBufferID : GLuint {};
		export enum class CopyReadBufferID : GLuint {};
		export enum class CopyWriteBufferID : GLuint {};
		export enum class ElementArrayBufferID : GLuint {};
		export enum class PixelPackBufferID : GLuint {};
		export enum class PixelUnpackBufferID : GLuint {};
		export enum class TextureBufferID : GLuint {};
		export enum class TransformFeedbackBufferID : GLuint {};
		export enum class UniformBufferID : GLuint {};

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

		template<IsBufferType Ty>
		struct DefaultDeleter<Ty>
		{
			void operator()(Ty id) { DeleteBuffer(id); }
		};

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

		export enum class BufferUsage : GLenum
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

		export enum class VertexArrayObjectID : GLuint {};

		void DeleteVertexArray(VertexArrayObjectID id)
		{
			GLuint untypedId = static_cast<GLuint>(id);
			glDeleteVertexArrays(1, &untypedId);
		}

		template<>
		struct DefaultDeleter<VertexArrayObjectID>
		{
			void operator()(VertexArrayObjectID id) { DeleteVertexArray(id); }
		};

	}

	namespace Rendering
	{
		export template<Device::IsTypedBuffer Ty>
		void BindBuffer(Ty id)
		{
			glBindBuffer(Device::bufferTypeToTarget<Ty>, static_cast<GLuint>(id));
		}

		export void BindVertexArray(Device::VertexArrayObjectID id)
		{
			glBindVertexArray(static_cast<GLuint>(id));
		}
	}


	namespace Device
	{
		export UniqueID<BufferID> GenBuffer()
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
		void RecreateBuffer(Ty id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			Rendering::BindBuffer(id);
			glBufferData(bufferTypeToTarget<Ty>, size, optionalInitialData, static_cast<GLenum>(usage));
		}

		template<IsTypedBuffer Ty>
		UniqueID<Ty> CreateBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			Ty typedID{ static_cast<GLuint>(id.release()) };
			RecreateBuffer(typedID, size, optionalInitialData, usage);
			return typedID;
		}

		template<IsTypedBuffer Ty>
		void UpdateBuffer(Ty id, GLintptr offset, GLsizeiptr size, const GLvoid* data)
		{
			Rendering::BindBuffer(id);
			glBufferSubData(bufferTypeToTarget<Ty>, offset, size, data);
		}

		export UniqueID<ArrayBufferID> CreateArrayBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<ArrayBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<ArrayBufferID> CreateArrayBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateArrayBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<ArrayBufferID> CreateArrayBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateArrayBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<ArrayBufferID> CreateArrayBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<CopyReadBufferID> CreateCopyReadBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<CopyReadBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
			UniqueID<CopyReadBufferID> CreateCopyReadBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateCopyReadBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<CopyReadBufferID> CreateCopyReadBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateCopyReadBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<CopyReadBufferID> CreateCopyReadBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<CopyWriteBufferID> CreateCopyWriteBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<CopyWriteBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<CopyWriteBufferID> CreateCopyWriteBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateCopyWriteBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<CopyWriteBufferID> CreateCopyWriteBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateCopyWriteBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
			UniqueID<CopyWriteBufferID> CreateCopyWriteBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<ElementArrayBufferID> CreateElementArrayBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<ElementArrayBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<ElementArrayBufferID> CreateElementArrayBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateElementArrayBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<ElementArrayBufferID> CreateElementArrayBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateElementArrayBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<ElementArrayBufferID> CreateElementArrayBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<PixelPackBufferID> CreatePixelPackBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<PixelPackBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<PixelPackBufferID> CreatePixelPackBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreatePixelPackBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<PixelPackBufferID> CreatePixelPackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreatePixelPackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
			UniqueID<PixelPackBufferID> CreatePixelPackBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<PixelUnpackBufferID> CreatePixelUnpackBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<PixelUnpackBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<PixelUnpackBufferID> CreatePixelUnpackBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreatePixelUnpackBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<PixelUnpackBufferID> CreatePixelUnpackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreatePixelUnpackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<PixelUnpackBufferID> CreatePixelUnpackBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<TextureBufferID> CreateTextureBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<TextureBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<TextureBufferID> CreateTextureBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateTextureBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<TextureBufferID> CreateTextureBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateTextureBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<TextureBufferID> CreateTextureBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<TransformFeedbackBufferID> CreateTransformFeedbackBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<TransformFeedbackBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<TransformFeedbackBufferID> CreateTransformFeedbackBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateTransformFeedbackBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<TransformFeedbackBufferID> CreateTransformFeedbackBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateTransformFeedbackBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<TransformFeedbackBufferID> CreateTransformFeedbackBuffer(const Ty& data, BufferUsage usage)
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

		export UniqueID<UniformBufferID> CreateUniformBuffer(UniqueID<BufferID> id, GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateBuffer<UniformBufferID>(std::move(id), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<UniformBufferID> CreateUniformBuffer(UniqueID<BufferID> id, const Ty& data, BufferUsage usage)
		{
			return CreateUniformBuffer(std::move(id), sizeof(Ty), &data, usage);
		}

		export UniqueID<UniformBufferID> CreateUniformBuffer(GLsizeiptr size, const GLvoid* optionalInitialData, BufferUsage usage)
		{
			return CreateUniformBuffer(GenBuffer(), size, optionalInitialData, usage);
		}

		export template<class Ty>
		UniqueID<UniformBufferID> CreateUniformBuffer(const Ty& data, BufferUsage usage)
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

		enum class ShaderType : GLenum
		{
			Vertex = GL_VERTEX_SHADER,
			Geometry = GL_GEOMETRY_SHADER,
			Fragment = GL_FRAGMENT_SHADER
		};

		export enum class VertexShaderID : GLuint {};
		export enum class GeometryShaderID : GLuint {};
		export enum class FragmentShaderID : GLuint {};

		template<class Ty>
		concept IsTypedShader = std::same_as<Ty, VertexShaderID> || std::same_as<Ty, GeometryShaderID> || std::same_as<Ty, FragmentShaderID>;

		export template<IsTypedShader Ty>
		void DeleteShader(Ty id)
		{
			glDeleteShader(static_cast<GLuint>(id));
		}

		template<IsTypedShader Ty>
		struct DefaultDeleter<Ty>
		{
			void operator()(Ty id) { DeleteShader(id); }
		};

		template<ShaderType Ty>
		struct ShaderEnumToType;

		template<>
		struct ShaderEnumToType<ShaderType::Vertex>
		{
			using type = VertexShaderID;
		};

		template<>
		struct ShaderEnumToType<ShaderType::Geometry>
		{
			using type = GeometryShaderID;
		};

		template<>
		struct ShaderEnumToType<ShaderType::Fragment>
		{
			using type = FragmentShaderID;
		};

		template<ShaderType Ty>
		using ToShaderType_t = ShaderEnumToType<Ty>::type;

		template<ShaderType Ty>
		ToShaderType_t<Ty> CreateShader()
		{
			return ToShaderType_t<Ty>{ glCreateShader(static_cast<GLenum>(Ty)) };
		}

		export template<IsTypedShader Ty, size_t Extent>
		void SetShaderSource(Ty shaderID, std::span<std::string_view, Extent> sources)
		{
			std::array<const char*, Extent> strings;
			std::array<GLint, Extent> stringSizes;
			for(size_t i = 0; i < Extent; i++)
			{
				stringSizes[i] = sources[i].size();
				strings[i] = sources[i].data();
			}

			glShaderSource(static_cast<GLenum>(shaderID), sources.size(), strings.data(), stringSizes.data());
		}

		export template<IsTypedShader Ty>
		void SetShaderSource(Ty shaderID, std::string_view sources)
		{
			std::array<std::string_view, 1> tempSources{ sources };
			SetShaderSource(shaderID, std::span{ tempSources });
		}

		export template<IsTypedShader Ty>
		void CompileShader(Ty shaderID)
		{
			glCompileShader(static_cast<GLenum>(shaderID));
		}


		export VertexShaderID CreateVertexShader() { return CreateShader<ShaderType::Vertex>(); }
		export VertexShaderID CreateAndCompileVertexShader(std::string_view sources)
		{
			VertexShaderID id = CreateVertexShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}
		export template<size_t Extent>
		VertexShaderID CreateAndCompileVertexShader(std::span<std::string_view, Extent> sources)
		{
			VertexShaderID id = CreateVertexShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}
		export GeometryShaderID CreateGeometryShader() { return CreateShader<ShaderType::Geometry>(); }
		export GeometryShaderID CreateAndCompileGeometryShader(std::string_view sources)
		{
			GeometryShaderID id = CreateGeometryShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}
		export template<size_t Extent>
		GeometryShaderID CreateAndCompileGeometryShader(std::span<std::string_view, Extent> sources)
		{
			GeometryShaderID id = CreateGeometryShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}
		export FragmentShaderID CreateFragmentShader() { return CreateShader<ShaderType::Fragment>(); }
		export FragmentShaderID CreateAndCompileFragmentShader(std::string_view sources)
		{
			FragmentShaderID id = CreateFragmentShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}
		export template<size_t Extent>
		FragmentShaderID CreateAndCompileFragmentShader(std::span<std::string_view, Extent> sources)
		{
			FragmentShaderID id = CreateFragmentShader();
			SetShaderSource(id, sources);
			CompileShader(id);
			return id;
		}

		export enum class ShaderProgramID : GLenum {};

		void DeleteProgram(ShaderProgramID id)
		{
			glDeleteProgram(static_cast<GLuint>(id));
		}

		template<>
		struct DefaultDeleter<ShaderProgramID>
		{
			void operator()(ShaderProgramID id) { DeleteProgram(id); }
		};

		export ShaderProgramID CreateShaderProgram(VertexShaderID* optionalVertexShader, GeometryShaderID* optionalGeometryShader, FragmentShaderID* optionalFragmentShader)
		{
			auto program = glCreateProgram();
			if(optionalVertexShader)
				glAttachShader(program, static_cast<GLuint>(*optionalVertexShader));
			if(optionalGeometryShader)
				glAttachShader(program, static_cast<GLuint>(*optionalGeometryShader));
			if(optionalFragmentShader)
				glAttachShader(program, static_cast<GLuint>(*optionalFragmentShader));
			glLinkProgram(program);
			return ShaderProgramID{ program };
		}

		export enum class VertexAttributeType : GLenum
		{
			Byte = GL_BYTE,
			Unsigned_Byte = GL_UNSIGNED_BYTE,
			Short = GL_SHORT,
			Unsigned_Short = GL_UNSIGNED_SHORT,
			Int = GL_INT,
			Unsigned_Int = GL_UNSIGNED_INT,
			Half_Float = GL_HALF_FLOAT,
			Float = GL_FLOAT,
			Double = GL_DOUBLE,
			Int_2_10_10_10_Rev = GL_INT_2_10_10_10_REV,
			Unsigned_Int_2_10_10_10_Rev = GL_UNSIGNED_INT_2_10_10_10_REV
		};

		export struct VertexAttribute
		{
			GLuint index;
			GLint size;
			VertexAttributeType type;
			bool normalized;
			GLsizei stride;
			GLintptr offset;
		};

		export void SetVertexAttributePointer(Device::VertexArrayObjectID id, std::span<VertexAttribute> attributes)
		{
			Rendering::BindVertexArray(id);
			for(const auto& attribute : attributes)
			{
				glVertexAttribPointer(attribute.index, attribute.size, static_cast<GLenum>(attribute.type), attribute.normalized, attribute.stride, reinterpret_cast<void*>(attribute.offset));
				glEnableVertexAttribArray(attribute.index);
			}
		}

		export template<size_t Extent>
		void SetVertexAttributePointer(Device::VertexArrayObjectID id, std::array<VertexAttribute, Extent> attributes)
		{
			SetVertexAttributePointer(id, std::span{ attributes });
		}

		export VertexArrayObjectID CreateVertexArrayObject()
		{
			GLuint id ;
			glGenVertexArrays(1, &id);
			return VertexArrayObjectID{ id };
		}

		export VertexArrayObjectID CreateVertexArrayObject(std::span<VertexAttribute> attributes)
		{
			auto id = CreateVertexArrayObject();
			SetVertexAttributePointer(id, attributes);
			return id;
		}

		export template<size_t Extent>
		VertexArrayObjectID CreateVertexArrayObject(std::array<VertexAttribute, Extent> attributes)
		{
			auto id = CreateVertexArrayObject();
			SetVertexAttributePointer(id, attributes);
			return id;
		}
	}

	namespace Rendering
	{
		export enum class ClearFlag : GLbitfield
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

		export enum class Feature : GLenum
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

		export void BindShaderProgram(Device::ShaderProgramID id)
		{
			glUseProgram(static_cast<GLuint>(id));
		}

		export enum class DrawMode : GLenum
		{
			Points = GL_POINTS,
			Line_Strip = GL_LINE_STRIP,
			Line_Loop = GL_LINE_LOOP,
			Lines = GL_LINES,
			Line_Strip_Adjaceny = GL_LINE_STRIP_ADJACENCY,
			Lines_Adjaceny = GL_LINES_ADJACENCY,
			Triangle_Strip = GL_TRIANGLE_STRIP,
			Triangle_Fan = GL_TRIANGLE_FAN,
			Triangles = GL_TRIANGLES,
			Triangle_Strip_Adjaceny = GL_TRIANGLE_STRIP_ADJACENCY,
			Triangles_Adjaceny = GL_TRIANGLES_ADJACENCY
		};

		export enum class ElementType : GLenum
		{
			Unsigned_Byte = GL_UNSIGNED_BYTE,
			Unsigned_Short = GL_UNSIGNED_SHORT,
			Unsigned_Int = GL_UNSIGNED_INT
		};

		export void DrawArrays(DrawMode mode, GLint first, GLsizei count)
		{
			glDrawArrays(static_cast<GLenum>(mode), first, count);
		}

		export void DrawElements(DrawMode mode, GLint count, ElementType type, const GLvoid* indices)
		{
			glDrawElements(static_cast<GLenum>(mode), count, static_cast<GLenum>(type), indices);
		}
	}
}
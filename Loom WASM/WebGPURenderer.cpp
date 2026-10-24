#ifdef __EMSCRIPTEN__

#include "Engine.h"
#include "GlslCompiler.h"
#include "Renderer.h"
#include "WebCanvas.h"
#include "Wgsl.h"

#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_wgpu.h"


#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr size_t RGBA = 4;

		constexpr wgpu::TextureFormat COLOR_TARGET_FORMAT = wgpu::TextureFormat::RGBA8Unorm;
		constexpr wgpu::TextureFormat DEPTH_FORMAT = wgpu::TextureFormat::Depth32Float;

		// Vertices, uniform blocks and what is copied into storage buffers are
		// written into chunks of this size, handed to the GPU before each submit.
		constexpr uint64_t CHUNK_SIZE = 4 * 1024 * 1024;

		// WebGPU copies, indices and storage buffers go a word at a time.
		constexpr uint64_t WORD = 4;

		// A uniform block is bound at a multiple of its largest member's size,
		// which std140 makes a vec4.
		constexpr uint64_t UNIFORM_SIZE_ALIGNMENT = 16;

		// What a texture read back has each of its rows padded to.
		constexpr uint32_t ROW_ALIGNMENT = 256;

		// Textures, storage and the uniform block in one group, and the samplers
		// Tint splits off the GLSL's combined ones at the same bindings in the other.
		constexpr uint32_t RESOURCE_GROUP = 0;
		constexpr uint32_t SAMPLER_GROUP = 1;

		constexpr uint32_t IMGUI_FRAMES_IN_FLIGHT = 3;

		constexpr uint32_t POSITION_LOCATION = 0;
		constexpr uint32_t UV_LOCATION = 1;
		constexpr uint32_t NORMAL_LOCATION = 2;
		constexpr uint32_t POSITION_COMPONENTS = 3;
		constexpr uint32_t UV_COMPONENTS = 2;
		constexpr uint32_t NORMAL_COMPONENTS = 3;
		constexpr uint32_t UV_OFFSET = POSITION_COMPONENTS * sizeof(float);
		constexpr uint32_t NORMAL_OFFSET = (POSITION_COMPONENTS + UV_COMPONENTS) * sizeof(float);
		constexpr uint32_t NORMAL_SIZE = NORMAL_COMPONENTS * sizeof(float);
		constexpr uint32_t INSTANCE_SIZE = Renderer::FloatsPerInstance * sizeof(float);
		constexpr uint32_t INSTANCE_COLUMNS = 4;
		constexpr uint32_t INSTANCE_COLUMN_SIZE = INSTANCE_COLUMNS * sizeof(float);

		constexpr float FAR_DEPTH = 1.0f;
		constexpr uint8_t WHITE = 0xFF;

		// What the vertex stage multiplies clip space y by, and the fragment
		// stage dFdy by, drawing into the window and into a texture.
		constexpr double UNFLIPPED = 1.0;
		constexpr double FLIPPED = -1.0;

		// SetBufferData's bytes that do not start and end on a word are merged
		// into the words around them by this, handed the first byte, the byte
		// after the last, then the words they fall in.
		constexpr uint32_t MERGE_WORKGROUP_SIZE = 64;
		constexpr uint32_t MERGE_HEADER_WORDS = 2;

		const std::string MERGE = std::string("const WORKGROUP = ") + std::to_string(MERGE_WORKGROUP_SIZE) + "u;\n"
			"@group(0) @binding(0) var<storage, read_write> destination: array<u32>;\n"
			"@group(0) @binding(1) var<storage, read> incoming: array<u32>;\n"
			"@compute @workgroup_size(WORKGROUP) fn main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u)\n"
			"{\n"
			"	let start = incoming[0];\n"
			"	let end = incoming[1];\n"
			"	let first = start / 4u;\n"
			"	let count = (end + 3u) / 4u - first;\n"
			"	for (var i = id.x; i < count; i += groups.x * WORKGROUP)\n"
			"	{\n"
			"		let word = first + i;\n"
			"		var mask = 0u;\n"
			"		for (var b = 0u; b < 4u; b++)\n"
			"		{\n"
			"			let byte = word * 4u + b;\n"
			"			if (byte >= start && byte < end) { mask |= 0xFFu << (b * 8u); }\n"
			"		}\n"
			"		destination[word] = (destination[word] & ~mask) | (incoming[2u + i] & mask);\n"
			"	}\n"
			"}\n";

		// The GL primitives Mesh serialises.
		enum GLPrimitive : uint32_t
		{
			GL_POINTS_ = 0x0000,
			GL_LINES_ = 0x0001,
			GL_LINE_LOOP_ = 0x0002,
			GL_LINE_STRIP_ = 0x0003,
			GL_TRIANGLES_ = 0x0004,
			GL_TRIANGLE_STRIP_ = 0x0005,
			GL_TRIANGLE_FAN_ = 0x0006,
		};

		// The GL usage hint for vertices set once and drawn many times.
		constexpr uint32_t GL_STATIC_DRAW_ = 0x88E4;

		// Looked up by the const char* a caller has, without building a string.
		struct NameHash
		{
			using is_transparent = void;

			size_t operator()(std::string_view name) const { return std::hash<std::string_view>{ }(name); };
		};

		template<typename Value>
		using ByName = std::unordered_map<std::string, Value, NameHash, std::equal_to<>>;

		wgpu::PrimitiveTopology Topology(uint32_t primitive)
		{
			switch (primitive)
			{
			case GL_POINTS_:			return wgpu::PrimitiveTopology::PointList;
			case GL_LINES_:				return wgpu::PrimitiveTopology::LineList;
			// WebGPU has no loop or fan: Draw closes a loop into a strip, and
			// indexes a fan as a list.
			case GL_LINE_LOOP_:
			case GL_LINE_STRIP_:		return wgpu::PrimitiveTopology::LineStrip;
			case GL_TRIANGLE_STRIP_:	return wgpu::PrimitiveTopology::TriangleStrip;
			default:					return wgpu::PrimitiveTopology::TriangleList;
			};
		};

		bool IsTriangles(wgpu::PrimitiveTopology topology)
		{
			return topology == wgpu::PrimitiveTopology::TriangleList || topology == wgpu::PrimitiveTopology::TriangleStrip;
		};

		uint64_t RoundUp(uint64_t value, uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		};

		wgpu::ShaderStage StagesOf(uint32_t stages)
		{
			wgpu::ShaderStage visibility = wgpu::ShaderStage::None;

			if (stages & Glsl::VERTEX_STAGE)
				visibility |= wgpu::ShaderStage::Vertex;

			if (stages & Glsl::FRAGMENT_STAGE)
				visibility |= wgpu::ShaderStage::Fragment;

			if (stages & Glsl::COMPUTE_STAGE)
				visibility |= wgpu::ShaderStage::Compute;

			return visibility;
		};
	};

	struct WebGPURenderer final : Renderer
	{
		WebGPURenderer()
		{
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		};

		Backend GetBackend() const override
		{
			return Backend::WebGPU;
		};

		bool Attach(GLFWwindow*) override
		{
			const wgpu::InstanceFeatureName features[] = { wgpu::InstanceFeatureName::TimedWaitAny };

			wgpu::InstanceDescriptor instance;
			instance.requiredFeatureCount = std::size(features);
			instance.requiredFeatures = features;
			m_instance = wgpu::CreateInstance(&instance);

			if (!m_instance)
				return false;

			Wait(m_instance.RequestAdapter(nullptr, wgpu::CallbackMode::WaitAnyOnly,
				[this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message)
				{
					if (status == wgpu::RequestAdapterStatus::Success)
						m_adapter = std::move(adapter);
					else std::cerr << "No WebGPU adapter: " << std::string_view(message) << std::endl;
				}));

			if (!m_adapter)
				return false;

			m_adapter.GetLimits(&m_limits);

			wgpu::DeviceDescriptor device;
			device.requiredLimits = &m_limits;
			device.SetUncapturedErrorCallback(
				[](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
				{
					std::cerr << "WebGPU: " << std::string_view(message) << std::endl;
				});

			Wait(m_adapter.RequestDevice(&device, wgpu::CallbackMode::WaitAnyOnly,
				[this](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
				{
					if (status == wgpu::RequestDeviceStatus::Success)
						m_device = std::move(device);
					else std::cerr << "No WebGPU device: " << std::string_view(message) << std::endl;
				}));

			if (!m_device)
				return false;

			m_queue = m_device.GetQueue();

			// Only now that WebGPU is sure to run, since a canvas that has given
			// out a WebGPU context cannot give out WebGL's to the fallback.
			wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
			canvas.selector = WebCanvas::SELECTOR;

			wgpu::SurfaceDescriptor surface;
			surface.nextInChain = &canvas;
			m_surface = m_instance.CreateSurface(&surface);

			wgpu::SurfaceCapabilities capabilities;
			m_surface.GetCapabilities(m_adapter, &capabilities);

			const wgpu::TextureFormat* formats = capabilities.formats;
			const wgpu::TextureFormat* const formatsEnd = formats + capabilities.formatCount;

			for (const wgpu::TextureFormat format : { wgpu::TextureFormat::BGRA8Unorm, wgpu::TextureFormat::RGBA8Unorm })
				if (std::find(formats, formatsEnd, format) != formatsEnd)
				{
					m_surfaceFormat = format;
					break;
				};

			if (m_surfaceFormat == wgpu::TextureFormat::Undefined)
				return false;

			WebCanvas::FollowResizes();

			m_linear = CreateSampler(wgpu::FilterMode::Linear, false);
			m_nearest = CreateSampler(wgpu::FilterMode::Nearest, false);
			m_shadow = CreateSampler(wgpu::FilterMode::Linear, true);

			wgpu::BufferDescriptor zeros;
			zeros.usage = wgpu::BufferUsage::Vertex;
			zeros.size = RoundUp(NORMAL_SIZE, UNIFORM_SIZE_ALIGNMENT);
			m_zeros = m_device.CreateBuffer(&zeros);

			const uint8_t white[] = { WHITE, WHITE, WHITE, WHITE };
			m_fallbackColor = CreateTexture(1, 1, white);
			m_fallbackDepth = CreateDepthTarget(1);

			wgpu::AdapterInfo info;
			m_adapter.GetInfo(&info);
			std::cout << "WebGPU Device: " << std::string_view(info.vendor) << ' ' << std::string_view(info.architecture) << std::endl;

			return m_fallbackColor && m_fallbackDepth;
		};

		void InitImGui(GLFWwindow* window) override
		{
			// The canvas's callbacks stand in for the window's.
			ImGui_ImplGlfw_InitForOther(window, false);
			WebCanvas::InstallImGui(window);

			ImGui_ImplWGPU_InitInfo info;
			info.Device = m_device.Get();
			info.NumFramesInFlight = IMGUI_FRAMES_IN_FLIGHT;
			info.RenderTargetFormat = (WGPUTextureFormat)m_surfaceFormat;
			info.DepthStencilFormat = WGPUTextureFormat_Undefined;
			ImGui_ImplWGPU_Init(&info);

			m_imgui = true;
		};

		void ShutdownImGui() override
		{
			if (!m_imgui)
				return;

			for (auto& [handle, texture] : m_textures)
				texture.imgui = false;

			ImGui_ImplWGPU_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			m_imguiViews.clear();
			m_imgui = false;
		};

		void NewImGuiFrame() override
		{
			ImGui_ImplWGPU_NewFrame();
			ImGui_ImplGlfw_NewFrame();
		};

		void RenderImGui(ImDrawData* drawData) override
		{
			if (!m_recording || !m_haveWindow)
				return;

			EndRendering();
			BeginRendering(WINDOW, false);
			ImGui_ImplWGPU_RenderDrawData(drawData, m_pass.Get());
			EndRendering();
		};

		void RenderImGuiWindows() override
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		};

		void BeginFrame() override
		{
			m_framesBegun++;

			const glm::ivec2 size = WebCanvas::Size();

			if (size != m_windowSize)
				Resize(size);

			ResetChunks();

			m_encoder = m_device.CreateCommandEncoder();
			m_recording = true;
			m_rendering = false;
			m_stack.assign(1, WINDOW);
			m_windowPending = Pending{ };
		};

		void EndFrame() override
		{
			if (!m_recording)
				return;

			EndRendering();

			Readback readback;

			if (m_haveWindow)
			{
				// A clear nothing was drawn after still has to land.
				if (m_windowPending.color || m_windowPending.depth)
				{
					m_stack.assign(1, WINDOW);
					BeginRendering(WINDOW, true);
					EndRendering();
				};

				Present();

				if (m_capture)
					readback = RecordReadback();
			};

			Submit(m_encoder);
			m_encoder = nullptr;
			m_recording = false;

			DeliverReadback(readback);
			DeliverReads();
		};

		void Capture(const CaptureHandler& handler) override
		{
			m_capture = handler;
		};

		uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) override
		{
			return AddProgram(name, Glsl::Compile(vertex, fragment), false);
		};

		void DeleteProgram(uint32_t handle) override
		{
			m_programs.erase(handle);
		};

		void SetUniform(uint32_t program, const char* name, float value) override
		{
			Write(program, name, &value, sizeof(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec3& value) override
		{
			Write(program, name, glm::value_ptr(value), sizeof(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec4& value) override
		{
			Write(program, name, glm::value_ptr(value), sizeof(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::mat4& value) override
		{
			Write(program, name, glm::value_ptr(value), sizeof(value));
		};

		void SetUniform(uint32_t handle, const char* name, UniformType type, const void* components) override
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			Program& program = found->second;
			const auto member = program.members.find(std::string_view(name));

			if (member == program.members.end())
				return;

			const UniformTypeInfo& info = InfoOf(type);
			const uint32_t column = info.rows * sizeof(uint32_t);
			const uint32_t stride = IsMatrix(info) ? member->second.matrixStride : column;
			const uint32_t end = member->second.offset + member->second.size;

			for (int i = 0; i < info.columns; i++)
			{
				const uint32_t offset = member->second.offset + i * stride;

				if (offset + column > end)
					break;

				memcpy(program.block.data() + offset, (const uint8_t*)components + i * column, column);
			};
		};

		void SetTexture(uint32_t program, const char* name, uint32_t texture) override
		{
			const auto found = m_programs.find(program);

			if (found == m_programs.end())
				return;

			const auto sampler = found->second.samplers.find(std::string_view(name));

			if (sampler != found->second.samplers.end())
				sampler->second.texture = texture;
		};

		bool SupportsCompute() const override
		{
			return true;
		};

		uint32_t CreateComputeProgram(const std::string& name, const std::string& compute) override
		{
			return AddProgram(name, Glsl::CompileCompute(compute), true);
		};

		void Dispatch(uint32_t handle, uint32_t x, uint32_t y, uint32_t z) override
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end() || !found->second.compute || !Ready(found->second))
				return;

			Program& program = found->second;

			Run([&](wgpu::CommandEncoder& encoder)
			{
				const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
				pass.SetPipeline(program.computePipeline);
				BindResources(pass, program);
				pass.DispatchWorkgroups(x, y, z);
				pass.End();
			});
		};

		uint32_t CreateBuffer(size_t size) override
		{
			if (size == 0)
				return 0;

			StorageBuffer storage;
			storage.size = size;

			// Starts zeroed, as WebGPU's buffers do.
			wgpu::BufferDescriptor descriptor;
			descriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
			descriptor.size = RoundUp(size, WORD);
			storage.buffer = m_device.CreateBuffer(&descriptor);

			if (!storage.buffer)
				return 0;

			const uint32_t handle = m_nextHandle++;
			m_buffers[handle] = std::move(storage);

			return handle;
		};

		void SetBufferData(uint32_t handle, const void* data, size_t size, size_t offset) override
		{
			const auto found = m_buffers.find(handle);

			if (found == m_buffers.end() || !size || offset + size > found->second.size)
				return;

			const wgpu::Buffer& buffer = found->second.buffer;

			if (offset % WORD || size % WORD)
				Merge(buffer, data, size, offset);
			else if (!m_recording)
				m_queue.WriteBuffer(buffer, offset, data, size);
			else
			{
				const Allocation staging = Allocate(size, WORD);
				memcpy(staging.mapped, data, size);

				Run([&](wgpu::CommandEncoder& encoder) { encoder.CopyBufferToBuffer(staging.buffer, staging.offset, buffer, offset, size); });
			};
		};

		void ReadBuffer(uint32_t handle, const BufferHandler& handler) override
		{
			const auto found = m_buffers.find(handle);

			if (found == m_buffers.end())
				return;

			Read read;
			read.size = found->second.size;
			read.handler = handler;

			wgpu::BufferDescriptor descriptor;
			descriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
			descriptor.size = RoundUp(read.size, WORD);
			read.buffer = m_device.CreateBuffer(&descriptor);

			const wgpu::Buffer& buffer = found->second.buffer;

			Run([&](wgpu::CommandEncoder& encoder) { encoder.CopyBufferToBuffer(buffer, 0, read.buffer, 0, descriptor.size); });

			m_reads.push_back(std::move(read));
		};

		void DestroyBuffer(uint32_t handle) override
		{
			if (m_buffers.erase(handle))
				ForgetBindGroups();
		};

		void SetBuffer(uint32_t handle, const char* block, uint32_t buffer) override
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			const auto binding = found->second.buffers.find(std::string_view(block));

			if (binding != found->second.buffers.end())
				binding->second.resource = buffer;
		};

		uint32_t CreateStorageTexture(int width, int height) override
		{
			Texture texture;
			texture.width = width;
			texture.height = height;
			texture.storage = true;
			texture.color = CreateTexture(COLOR_TARGET_FORMAT, width, height, wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding);

			if (!texture.color)
				return 0;

			texture.colorView = texture.color.CreateView();

			return Add(std::move(texture));
		};

		void SetImage(uint32_t handle, const char* name, uint32_t texture) override
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			const auto image = found->second.images.find(std::string_view(name));

			if (image != found->second.images.end())
				image->second.resource = texture;
		};

		void Draw(uint32_t handle, uint32_t primitive, const float* vertices, size_t count, uint32_t, VertexFormat format) override
		{
			DrawWith(handle, primitive, count, format, nullptr, 1,
				[&](uint32_t slot, size_t drawn, size_t stride)
				{
					BindStreamed(slot, vertices, count, drawn, stride);
				});
		};

		// Hands count vertices over for this draw alone, drawn of them.
		void BindStreamed(uint32_t slot, const float* vertices, size_t count, size_t drawn, size_t stride)
		{
			const Allocation vertexData = Allocate(drawn * stride, WORD);

			// None reads zeros, as GL does with its arrays off.
			if (!vertices)
				memset(vertexData.mapped, 0, drawn * stride);
			else
			{
				memcpy(vertexData.mapped, vertices, count * stride);

				if (drawn > count)
					memcpy(vertexData.mapped + count * stride, vertices, stride);
			};

			m_pass.SetVertexBuffer(slot, vertexData.buffer, vertexData.offset, drawn * stride);
		};

		uint32_t CreateVertices() override
		{
			const uint32_t handle = m_nextKept++;
			m_kept[handle] = { };

			return handle;
		};

		void SetVertices(uint32_t handle, const float* data, size_t count, uint32_t usage, VertexFormat format) override
		{
			const auto found = m_kept.find(handle);

			if (found == m_kept.end())
				return;

			Kept& kept = found->second;
			Release(kept);

			kept.count = count;
			kept.format = format;
			kept.streamed.assign(data, data + count * FloatsPerVertex(format));
			kept.settles = usage == GL_STATIC_DRAW_ && count > 0;
			kept.setOn = m_framesBegun;
		};

		void DrawVertices(uint32_t program, uint32_t primitive, uint32_t handle) override
		{
			DrawKept(program, primitive, handle, nullptr, 1);
		};

		void DrawInstances(uint32_t program, uint32_t primitive, uint32_t vertices, const float* instances, size_t count) override
		{
			DrawKept(program, primitive, vertices, instances, count);
		};

		// Draws kept vertices once, or once for each of instanceCount instances
		// when there are instances.
		void DrawKept(uint32_t program, uint32_t primitive, uint32_t handle, const float* instances, size_t instanceCount)
		{
			const auto found = m_kept.find(handle);

			if (found == m_kept.end())
				return;

			Kept& kept = found->second;

			// Only vertices set to stay that have also stayed a whole frame are
			// moved, and any still changing are handed over afresh with each
			// draw, as Draw's are.
			if (!kept.buffer && kept.settles && kept.setOn != m_framesBegun)
				Settle(kept);

			DrawWith(program, primitive, kept.count, kept.format, instances, instanceCount,
				[&](uint32_t slot, size_t drawn, size_t stride)
				{
					if (kept.buffer)
						m_pass.SetVertexBuffer(slot, kept.buffer);
					else BindStreamed(slot, kept.streamed.data(), kept.count, drawn, stride);
				});
		};

		void DestroyVertices(uint32_t handle) override
		{
			const auto found = m_kept.find(handle);

			if (found == m_kept.end())
				return;

			Release(found->second);
			m_kept.erase(found);
		};

		void SetPixelated(uint32_t handle, bool pixelated) override
		{
			const auto found = m_programs.find(handle);

			if (found != m_programs.end())
				found->second.pixelated = pixelated;
		};

		uint32_t CreateTexture(int width, int height, const uint8_t* rgba) override
		{
			Texture texture;
			texture.width = width;
			texture.height = height;
			texture.color = CreateTexture(COLOR_TARGET_FORMAT, width, height, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);

			if (!texture.color)
				return 0;

			texture.colorView = texture.color.CreateView();

			if (rgba)
			{
				wgpu::TexelCopyTextureInfo destination;
				destination.texture = texture.color;

				wgpu::TexelCopyBufferLayout layout;
				layout.bytesPerRow = (uint32_t)(width * RGBA);
				layout.rowsPerImage = (uint32_t)height;

				const wgpu::Extent3D extent = Extent(width, height);
				m_queue.WriteTexture(&destination, rgba, (size_t)width * height * RGBA, &layout, &extent);
			};

			return Add(std::move(texture));
		};

		uint32_t CreateDepthTarget(int size) override
		{
			Texture texture;
			texture.width = size;
			texture.height = size;
			texture.depth = CreateTexture(DEPTH_FORMAT, size, size, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);

			if (!texture.depth)
			{
				std::cerr << "A " << size << " square depth target could not be allocated" << std::endl;
				return 0;
			};

			texture.depthView = texture.depth.CreateView();

			// Starts at the far plane, so a map nothing has been drawn into yet
			// puts nothing in shadow.
			ClearDepth(texture.depthView);

			return Add(std::move(texture));
		};

		uint32_t CreateColorTarget(int width, int height) override
		{
			Texture texture;
			texture.width = width;
			texture.height = height;
			texture.color = CreateTexture(COLOR_TARGET_FORMAT, width, height, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
			texture.depth = CreateTexture(DEPTH_FORMAT, width, height, wgpu::TextureUsage::RenderAttachment);

			if (!texture.color || !texture.depth)
			{
				std::cerr << "A " << width << 'x' << height << " render target could not be allocated" << std::endl;
				return 0;
			};

			texture.colorView = texture.color.CreateView();
			texture.depthView = texture.depth.CreateView();
			ClearDepth(texture.depthView);

			return Add(std::move(texture));
		};

		void DestroyTexture(uint32_t handle) override
		{
			const auto found = m_textures.find(handle);

			if (found == m_textures.end())
				return;

			// ImGui keeps what it drew a view with by the view's address, so the
			// view is kept for as long as ImGui is, and no other takes its place.
			if (found->second.imgui)
				m_imguiViews.push_back(found->second.colorView);

			m_textures.erase(found);
			ForgetBindGroups();
		};

		void PushTarget(uint32_t target) override
		{
			EndRendering();

			const auto found = m_textures.find(target);
			m_stack.push_back(found != m_textures.end() && !found->second.storage ? target : WINDOW);
		};

		void PopTarget() override
		{
			if (m_stack.size() <= 1)
				return;

			const Pending* const pending = PendingOf(m_stack.back());

			// A clear with nothing drawn after it still has to land.
			if (m_recording && pending && (pending->color || pending->depth))
				EnsureRendering(m_stack.back());

			EndRendering();
			m_stack.pop_back();
		};

		void Clear(const float* color, bool depth) override
		{
			Pending* const pending = PendingOf(m_stack.back());

			if (!pending)
				return;

			if (color)
			{
				pending->color = true;
				std::copy(color, color + std::size(pending->rgba), pending->rgba);
			};

			pending->depth |= depth;

			// Mid-pass, the clear is done by the load of the pass after.
			EndRendering();
		};

		void SetDepthBias(float slope, float constant) override
		{
			m_biasSlope = slope;
			m_biasConstant = constant;
		};

		void SetColorWrite(bool write) override
		{
			m_colorWrite = write;
		};

		void SetDepthTestEqual(bool equal) override
		{
			m_depthEqual = equal;
		};

		glm::ivec2 TargetSize() const override
		{
			const uint32_t target = m_stack.back();

			if (target == WINDOW)
				return m_windowSize;

			const Texture& texture = m_textures.at(target);

			return glm::ivec2(texture.width, texture.height);
		};

		int MaxTextureSize() const override
		{
			return (int)m_limits.maxTextureDimension2D;
		};

		void* ImGuiTexture(uint32_t handle) override
		{
			const auto found = m_textures.find(handle);

			if (!m_imgui || found == m_textures.end() || !found->second.colorView)
				return nullptr;

			found->second.imgui = true;

			return (void*)(intptr_t)found->second.colorView.Get();
		};

	private:
		// The window, as a target.
		static constexpr uint32_t WINDOW = 0;

		// A clear asked for before anything was drawn, done by the pass's load.
		struct Pending
		{
			bool color = false;
			bool depth = false;
			float rgba[RGBA] = { };
		};

		// A plain texture has only colour, a depth target only depth, and a
		// colour target both. Whichever is there first is what a shader samples.
		struct Texture
		{
			wgpu::Texture color;
			wgpu::Texture depth;
			wgpu::TextureView colorView;
			wgpu::TextureView depthView;
			int width = 0;
			int height = 0;
			Pending pending;
			bool imgui = false;
			bool storage = false;

			const wgpu::TextureView& Sampled() const { return colorView ? colorView : depthView; };
		};

		struct BindGroups
		{
			wgpu::BindGroup resources;
			wgpu::BindGroup samplers;
		};

		struct Program
		{
			struct Sampler
			{
				uint32_t binding;
				bool shadow;
				uint32_t texture = 0;
			};

			struct Storage
			{
				uint32_t binding;
				uint32_t resource = 0;
			};

			std::string name;
			bool compute = false;
			wgpu::ShaderModule vertex;
			wgpu::ShaderModule fragment;

			// Whether each stage has the constant that flips it for a texture.
			bool vertexFlips = false;
			bool fragmentFlips = false;

			bool hasPosition = false;
			bool hasUV = false;
			bool hasNormal = false;
			bool hasInstance = false;
			bool pixelated = false;

			uint32_t blockSize = 0;
			std::vector<uint8_t> block;
			ByName<Glsl::Layout::Member> members;
			ByName<Sampler> samplers;
			ByName<Storage> buffers;
			ByName<Storage> images;

			wgpu::BindGroupLayout resourceLayout;
			wgpu::BindGroupLayout samplerLayout;
			wgpu::PipelineLayout layout;
			wgpu::ComputePipeline computePipeline;

			// By the topology, the colour format, whether there is depth, whether
			// the target is a texture, whether colour is written, the vertex
			// format, whether depth is tested for equal, and the depth bias.
			std::map<std::array<uint32_t, 9>, wgpu::RenderPipeline> pipelines;

			// By the chunk the uniforms are in, then what each sampler, storage
			// block and image is set to.
			std::map<std::vector<uint32_t>, BindGroups> bindGroups;
		};

		struct StorageBuffer
		{
			wgpu::Buffer buffer;
			size_t size = 0;
		};

		// What ReadBuffer copied a buffer into, handed over once the frame is done.
		struct Read
		{
			wgpu::Buffer buffer;
			size_t size = 0;
			BufferHandler handler;
		};

		// The window copied out for Capture.
		struct Readback
		{
			wgpu::Buffer buffer;
			uint32_t rowBytes = 0;
		};

		// What CreateVertices handed out: vertices in a buffer of their own on
		// the GPU, or as they were handed over until then. settles says they
		// were set to stay, and setOn is the frame they were set in.
		struct Kept
		{
			wgpu::Buffer buffer;
			std::vector<float> streamed;
			size_t count = 0;
			VertexFormat format = VertexFormat::Position;
			bool settles = false;
			unsigned long long setOn = 0;
		};

		// What is written here before a submit is copied to buffer by it.
		struct Chunk
		{
			wgpu::Buffer buffer;
			std::vector<uint8_t> written;
			uint64_t used = 0;
			uint64_t dirtyStart = 0;
			uint64_t dirtyEnd = 0;
		};

		struct Allocation
		{
			uint32_t chunk;
			wgpu::Buffer buffer;
			uint64_t offset;
			uint8_t* mapped;
		};

		// The vertex buffer slots a pipeline reads, -1 for one it has none of.
		struct Slots
		{
			int vertices = -1;
			int noNormal = -1;
			int instances = -1;
		};

		template<typename Future>
		void Wait(const Future& future)
		{
			m_instance.WaitAny(future, UINT64_MAX);
		};

		wgpu::Sampler CreateSampler(wgpu::FilterMode filter, bool compare)
		{
			wgpu::SamplerDescriptor descriptor;
			descriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
			descriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
			descriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
			descriptor.magFilter = filter;
			descriptor.minFilter = filter;
			descriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;

			if (compare)
				descriptor.compare = wgpu::CompareFunction::LessEqual;

			return m_device.CreateSampler(&descriptor);
		};

		static wgpu::Extent3D Extent(int width, int height)
		{
			wgpu::Extent3D extent;
			extent.width = (uint32_t)width;
			extent.height = (uint32_t)height;

			return extent;
		};

		wgpu::Texture CreateTexture(wgpu::TextureFormat format, int width, int height, wgpu::TextureUsage usage)
		{
			if (width <= 0 || height <= 0 || width > MaxTextureSize() || height > MaxTextureSize())
				return nullptr;

			wgpu::TextureDescriptor descriptor;
			descriptor.usage = usage;
			descriptor.size = Extent(width, height);
			descriptor.format = format;

			return m_device.CreateTexture(&descriptor);
		};

		void ClearDepth(const wgpu::TextureView& view)
		{
			Run([&](wgpu::CommandEncoder& encoder)
			{
				wgpu::RenderPassDepthStencilAttachment depth;
				depth.view = view;
				depth.depthLoadOp = wgpu::LoadOp::Clear;
				depth.depthStoreOp = wgpu::StoreOp::Store;
				depth.depthClearValue = FAR_DEPTH;

				// { } zeroes the colour attachment count, which has no default.
				wgpu::RenderPassDescriptor pass{ };
				pass.depthStencilAttachment = &depth;
				encoder.BeginRenderPass(&pass).End();
			});
		};

		uint32_t Add(Texture&& texture)
		{
			const uint32_t handle = m_nextHandle++;
			m_textures[handle] = std::move(texture);

			return handle;
		};

		void Resize(glm::ivec2 size)
		{
			m_windowSize = size;
			m_haveWindow = size.x > 0 && size.y > 0;
			m_windowTexture = nullptr;
			m_windowColor = nullptr;
			m_windowDepth = nullptr;

			if (!m_haveWindow)
				return;

			wgpu::SurfaceConfiguration configuration;
			configuration.device = m_device;
			configuration.format = m_surfaceFormat;
			configuration.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
			configuration.width = (uint32_t)size.x;
			configuration.height = (uint32_t)size.y;
			configuration.alphaMode = wgpu::CompositeAlphaMode::Opaque;
			configuration.presentMode = wgpu::PresentMode::Fifo;
			m_surface.Configure(&configuration);

			// Drawn into rather than the canvas's own texture, which is only
			// there for the frame it is handed out for, and copied to it at the end.
			m_windowTexture = CreateTexture(m_surfaceFormat, size.x, size.y, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc);
			m_windowColor = m_windowTexture.CreateView();
			m_windowDepth = CreateTexture(DEPTH_FORMAT, size.x, size.y, wgpu::TextureUsage::RenderAttachment).CreateView();
		};

		void Present()
		{
			wgpu::SurfaceTexture current;
			m_surface.GetCurrentTexture(&current);

			if (current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
				current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
				return;

			wgpu::TexelCopyTextureInfo source;
			source.texture = m_windowTexture;

			wgpu::TexelCopyTextureInfo destination;
			destination.texture = current.texture;

			const wgpu::Extent3D extent = Extent(m_windowSize.x, m_windowSize.y);
			m_encoder.CopyTextureToTexture(&source, &destination, &extent);
		};

		Pending* PendingOf(uint32_t target)
		{
			if (target == WINDOW)
				return &m_windowPending;

			const auto found = m_textures.find(target);

			return found != m_textures.end() ? &found->second.pending : nullptr;
		};

		void EnsureRendering(uint32_t target)
		{
			if (m_rendering && m_renderingTarget == target && m_renderingDepth)
				return;

			EndRendering();
			BeginRendering(target, true);
		};

		// Starts drawing into a target, loading what is in it or clearing it
		// to what Clear asked for.
		void BeginRendering(uint32_t target, bool withDepth)
		{
			const wgpu::TextureView* color = &m_windowColor;
			const wgpu::TextureView* depth = &m_windowDepth;
			wgpu::TextureFormat format = m_surfaceFormat;
			Pending* const pending = PendingOf(target);

			if (target != WINDOW)
			{
				const Texture& texture = m_textures.at(target);
				color = &texture.colorView;
				depth = &texture.depthView;
				format = texture.colorView ? COLOR_TARGET_FORMAT : wgpu::TextureFormat::Undefined;
			};

			wgpu::RenderPassColorAttachment colorAttachment;
			wgpu::RenderPassDepthStencilAttachment depthAttachment;
			wgpu::RenderPassDescriptor pass{ };

			if (*color)
			{
				colorAttachment.view = *color;
				colorAttachment.loadOp = pending->color ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
				colorAttachment.storeOp = wgpu::StoreOp::Store;
				colorAttachment.clearValue = { pending->rgba[0], pending->rgba[1], pending->rgba[2], pending->rgba[3] };
				pending->color = false;

				pass.colorAttachmentCount = 1;
				pass.colorAttachments = &colorAttachment;
			};

			m_renderingDepth = withDepth && *depth;

			if (m_renderingDepth)
			{
				depthAttachment.view = *depth;
				depthAttachment.depthLoadOp = pending->depth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
				depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
				depthAttachment.depthClearValue = FAR_DEPTH;
				pending->depth = false;

				pass.depthStencilAttachment = &depthAttachment;
			};

			m_pass = m_encoder.BeginRenderPass(&pass);
			m_rendering = true;
			m_renderingTarget = target;
			m_renderingFormat = format;
		};

		void EndRendering()
		{
			if (!m_rendering)
				return;

			m_pass.End();
			m_pass = nullptr;
			m_rendering = false;
		};

		// Builds a program from its compiled stages, throwing what glslang,
		// Reflect or Tint refused it for.
		uint32_t AddProgram(const std::string& name, const Glsl::Program& compiled, bool compute)
		{
			if (!compiled.compiled)
				throw std::runtime_error("Shader compile error in " + name + ":\n" + compiled.log);

			const Glsl::Layout layout = Glsl::Reflect(name, compiled);

			Program program;
			program.name = name;
			program.compute = compute;
			program.blockSize = layout.blockSize;
			program.block.assign(layout.blockSize, 0);

			std::vector<wgpu::BindGroupLayoutEntry> resources;
			std::vector<wgpu::BindGroupLayoutEntry> samplers;
			std::vector<uint32_t> samplerBindings;

			if (layout.blockSize)
			{
				wgpu::BindGroupLayoutEntry& entry = resources.emplace_back();
				entry.binding = Glsl::DEFAULT_BLOCK_BINDING;
				entry.visibility = StagesOf(layout.blockStages);
				entry.buffer.type = wgpu::BufferBindingType::Uniform;
				entry.buffer.hasDynamicOffset = true;
			};

			for (const auto& [memberName, member] : layout.members)
				program.members.emplace(memberName, member);

			for (const auto& [samplerName, sampler] : layout.samplers)
			{
				program.samplers.emplace(samplerName, Program::Sampler{ sampler.binding, sampler.shadow });
				samplerBindings.push_back(sampler.binding);

				wgpu::BindGroupLayoutEntry& texture = resources.emplace_back();
				texture.binding = sampler.binding;
				texture.visibility = StagesOf(sampler.stages);
				texture.texture.sampleType = sampler.shadow ? wgpu::TextureSampleType::Depth : wgpu::TextureSampleType::Float;
				texture.texture.viewDimension = wgpu::TextureViewDimension::e2D;

				wgpu::BindGroupLayoutEntry& filter = samplers.emplace_back();
				filter.binding = sampler.binding;
				filter.visibility = texture.visibility;
				filter.sampler.type = sampler.shadow ? wgpu::SamplerBindingType::Comparison : wgpu::SamplerBindingType::Filtering;
			};

			for (const auto& [blockName, buffer] : layout.buffers)
			{
				program.buffers.emplace(blockName, Program::Storage{ buffer.binding });

				wgpu::BindGroupLayoutEntry& entry = resources.emplace_back();
				entry.binding = buffer.binding;
				entry.visibility = StagesOf(buffer.stages);
				entry.buffer.type = buffer.readonly ? wgpu::BufferBindingType::ReadOnlyStorage : wgpu::BufferBindingType::Storage;
			};

			for (const auto& [imageName, image] : layout.images)
			{
				program.images.emplace(imageName, Program::Storage{ image.binding });

				wgpu::BindGroupLayoutEntry& entry = resources.emplace_back();
				entry.binding = image.binding;
				entry.visibility = wgpu::ShaderStage::Compute;
				entry.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
				entry.storageTexture.format = COLOR_TARGET_FORMAT;
				entry.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
			};

			for (const uint32_t input : layout.inputs)
			{
				program.hasPosition |= input == POSITION_LOCATION;
				program.hasUV |= input == UV_LOCATION;
				program.hasNormal |= input == NORMAL_LOCATION;
				program.hasInstance |= input >= InstanceModelLocation && input <= InstanceColorLocation;
			};

			std::vector<wgpu::ShaderModule> modules;
			std::vector<bool> flips;

			for (const std::vector<uint32_t>& stage : compiled.spirv)
			{
				std::string wgsl;

				try
				{
					wgsl = Wgsl::FromSpirv(stage, samplerBindings, SAMPLER_GROUP);
				}
				catch (const std::runtime_error& error)
				{
					throw std::runtime_error("Shader compile error in " + name + ":\n" + error.what());
				};

				const uint32_t flipId = modules.empty() && !compute ? Glsl::CLIP_FLIP_CONSTANT_ID : Glsl::FLIP_CONSTANT_ID;
				flips.push_back(wgsl.find("@id(" + std::to_string(flipId) + ")") != std::string::npos);

				wgpu::ShaderSourceWGSL source;
				source.code = wgsl.c_str();

				wgpu::ShaderModuleDescriptor descriptor;
				descriptor.nextInChain = &source;
				modules.push_back(m_device.CreateShaderModule(&descriptor));
			};

			wgpu::BindGroupLayoutDescriptor resourceLayout;
			resourceLayout.entryCount = resources.size();
			resourceLayout.entries = resources.data();
			program.resourceLayout = m_device.CreateBindGroupLayout(&resourceLayout);

			std::vector<wgpu::BindGroupLayout> groups = { program.resourceLayout };

			if (!samplers.empty())
			{
				wgpu::BindGroupLayoutDescriptor samplerLayout;
				samplerLayout.entryCount = samplers.size();
				samplerLayout.entries = samplers.data();
				program.samplerLayout = m_device.CreateBindGroupLayout(&samplerLayout);
				groups.push_back(program.samplerLayout);
			};

			wgpu::PipelineLayoutDescriptor pipelineLayout;
			pipelineLayout.bindGroupLayoutCount = groups.size();
			pipelineLayout.bindGroupLayouts = groups.data();
			program.layout = m_device.CreatePipelineLayout(&pipelineLayout);

			if (compute)
			{
				wgpu::ComputePipelineDescriptor descriptor;
				descriptor.layout = program.layout;
				descriptor.compute.module = modules[0];
				descriptor.compute.entryPoint = "main";
				program.computePipeline = m_device.CreateComputePipeline(&descriptor);
			}
			else
			{
				program.vertex = modules[0];
				program.fragment = modules[1];
				program.vertexFlips = flips[0];
				program.fragmentFlips = flips[1];
			};

			const uint32_t handle = m_nextHandle++;
			m_programs[handle] = std::move(program);

			return handle;
		};

		// Whether every storage block and image the program has is set to a
		// buffer or storage texture that is still there.
		bool Ready(const Program& program) const
		{
			for (const auto& [name, buffer] : program.buffers)
				if (!m_buffers.contains(buffer.resource))
					return false;

			for (const auto& [name, image] : program.images)
			{
				const auto found = m_textures.find(image.resource);

				if (found == m_textures.end() || !found->second.storage)
					return false;
			};

			return true;
		};

		// Whatever is behind it has to match the sampler's kind: a shadow
		// sampler reads a depth texture.
		uint32_t SampledTexture(const Program::Sampler& sampler) const
		{
			const auto found = m_textures.find(sampler.texture);

			if (found != m_textures.end() && !found->second.colorView == sampler.shadow)
				return sampler.texture;

			return sampler.shadow ? m_fallbackDepth : m_fallbackColor;
		};

		// The program's uniforms as they are now, and the group of what it reads.
		template<typename Pass>
		void BindResources(const Pass& pass, Program& program)
		{
			std::vector<uint32_t> key;
			Allocation uniforms{ };
			const uint64_t uniformSize = RoundUp(program.blockSize, UNIFORM_SIZE_ALIGNMENT);

			if (program.blockSize)
			{
				uniforms = Allocate(uniformSize, m_limits.minUniformBufferOffsetAlignment);
				memcpy(uniforms.mapped, program.block.data(), program.blockSize);
				key.push_back(uniforms.chunk);
			};

			for (const auto& [name, sampler] : program.samplers)
				key.push_back(SampledTexture(sampler) << 1 | (program.pixelated && !sampler.shadow));

			for (const auto& [name, buffer] : program.buffers)
				key.push_back(buffer.resource);

			for (const auto& [name, image] : program.images)
				key.push_back(image.resource);

			auto found = program.bindGroups.find(key);

			if (found == program.bindGroups.end())
				found = program.bindGroups.emplace(key, CreateBindGroups(program, uniforms.buffer, uniformSize)).first;

			const uint32_t offset = (uint32_t)uniforms.offset;
			pass.SetBindGroup(RESOURCE_GROUP, found->second.resources, program.blockSize ? 1 : 0, &offset);

			if (found->second.samplers)
				pass.SetBindGroup(SAMPLER_GROUP, found->second.samplers);
		};

		BindGroups CreateBindGroups(const Program& program, const wgpu::Buffer& uniforms, uint64_t uniformSize)
		{
			std::vector<wgpu::BindGroupEntry> resources;
			std::vector<wgpu::BindGroupEntry> samplers;

			if (program.blockSize)
			{
				wgpu::BindGroupEntry& entry = resources.emplace_back();
				entry.binding = Glsl::DEFAULT_BLOCK_BINDING;
				entry.buffer = uniforms;
				entry.size = uniformSize;
			};

			for (const auto& [name, sampler] : program.samplers)
			{
				wgpu::BindGroupEntry& texture = resources.emplace_back();
				texture.binding = sampler.binding;
				texture.textureView = m_textures.at(SampledTexture(sampler)).Sampled();

				// What a pixelated program samples its colour textures through.
				wgpu::BindGroupEntry& filter = samplers.emplace_back();
				filter.binding = sampler.binding;
				filter.sampler = sampler.shadow ? m_shadow : program.pixelated ? m_nearest : m_linear;
			};

			for (const auto& [name, buffer] : program.buffers)
			{
				wgpu::BindGroupEntry& entry = resources.emplace_back();
				entry.binding = buffer.binding;
				entry.buffer = m_buffers.at(buffer.resource).buffer;
			};

			for (const auto& [name, image] : program.images)
			{
				wgpu::BindGroupEntry& entry = resources.emplace_back();
				entry.binding = image.binding;
				entry.textureView = m_textures.at(image.resource).colorView;
			};

			BindGroups groups;

			wgpu::BindGroupDescriptor resourceGroup;
			resourceGroup.layout = program.resourceLayout;
			resourceGroup.entryCount = resources.size();
			resourceGroup.entries = resources.data();
			groups.resources = m_device.CreateBindGroup(&resourceGroup);

			if (program.samplerLayout)
			{
				wgpu::BindGroupDescriptor samplerGroup;
				samplerGroup.layout = program.samplerLayout;
				samplerGroup.entryCount = samplers.size();
				samplerGroup.entries = samplers.data();
				groups.samplers = m_device.CreateBindGroup(&samplerGroup);
			};

			return groups;
		};

		// Bind groups hold what they bind, so they go when any of it might.
		void ForgetBindGroups()
		{
			for (auto& [handle, program] : m_programs)
				program.bindGroups.clear();
		};

		Slots SlotsOf(const Program& program, VertexFormat format) const
		{
			Slots slots;
			int next = 0;

			if (program.hasPosition || program.hasUV || (program.hasNormal && format == VertexFormat::PositionUVNormal))
				slots.vertices = next++;

			if (program.hasNormal && format != VertexFormat::PositionUVNormal)
				slots.noNormal = next++;

			if (program.hasInstance)
				slots.instances = next++;

			return slots;
		};

		template<typename Bind>
		void DrawWith(uint32_t handle, uint32_t primitive, size_t count, VertexFormat format, const float* instances, size_t instanceCount, const Bind& bind)
		{
			if (!m_recording || count == 0 || instanceCount == 0)
				return;

			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			Program& program = found->second;
			const uint32_t target = m_stack.back();

			if ((target == WINDOW && !m_haveWindow) || program.compute || !Ready(program))
				return;

			EnsureRendering(target);

			const wgpu::RenderPipeline pipeline = PipelineFor(program, primitive, format);

			if (!pipeline)
				return;

			m_pass.SetPipeline(pipeline);
			BindResources(m_pass, program);

			const Slots slots = SlotsOf(program, format);

			// A loop is a strip with its first vertex again at the end.
			const size_t drawn = primitive == GL_LINE_LOOP_ ? count + 1 : count;
			const size_t stride = FloatsPerVertex(format) * sizeof(float);

			if (slots.vertices >= 0)
				bind((uint32_t)slots.vertices, drawn, stride);

			if (slots.noNormal >= 0)
				m_pass.SetVertexBuffer((uint32_t)slots.noNormal, m_zeros);

			if (slots.instances >= 0)
			{
				const Allocation instanceData = Allocate(instanceCount * INSTANCE_SIZE, WORD);

				if (instances)
					memcpy(instanceData.mapped, instances, instanceCount * INSTANCE_SIZE);
				else memset(instanceData.mapped, 0, instanceCount * INSTANCE_SIZE);

				m_pass.SetVertexBuffer((uint32_t)slots.instances, instanceData.buffer, instanceData.offset, instanceCount * INSTANCE_SIZE);
			};

			if (primitive != GL_TRIANGLE_FAN_)
			{
				m_pass.Draw((uint32_t)drawn, (uint32_t)instanceCount);
				return;
			};

			// A fan is a triangle from its first vertex to each pair after it.
			constexpr uint32_t CORNERS = 3;
			const uint32_t triangles = (uint32_t)count - (CORNERS - 1);

			if (count < CORNERS)
				return;

			const Allocation indices = Allocate(triangles * CORNERS * sizeof(uint32_t), WORD);
			uint32_t* const index = (uint32_t*)indices.mapped;

			for (uint32_t i = 0; i < triangles; i++)
			{
				index[i * CORNERS] = 0;
				index[i * CORNERS + 1] = i + 1;
				index[i * CORNERS + 2] = i + 2;
			};

			m_pass.SetIndexBuffer(indices.buffer, wgpu::IndexFormat::Uint32, indices.offset, triangles * CORNERS * sizeof(uint32_t));
			m_pass.DrawIndexed(triangles * CORNERS, (uint32_t)instanceCount);
		};

		// Without texture coordinates, aUV reads the position's x and y, and
		// without normals, aNormal reads one normal of zeros for the whole draw,
		// as they do under GL.
		wgpu::RenderPipeline PipelineFor(Program& program, uint32_t primitive, VertexFormat format)
		{
			const wgpu::PrimitiveTopology topology = Topology(primitive);
			const bool flipped = m_renderingTarget != WINDOW;

			const std::array<uint32_t, 9> key =
			{
				(uint32_t)topology,
				(uint32_t)m_renderingFormat,
				m_renderingDepth,
				flipped,
				m_colorWrite,
				(uint32_t)format,
				m_depthEqual,
				std::bit_cast<uint32_t>(m_biasSlope),
				std::bit_cast<uint32_t>(m_biasConstant),
			};

			const auto found = program.pipelines.find(key);

			if (found != program.pipelines.end())
				return found->second;

			const Slots slots = SlotsOf(program, format);
			std::vector<wgpu::VertexBufferLayout> buffers;
			std::vector<wgpu::VertexAttribute> vertexAttributes;

			if (program.hasPosition)
				vertexAttributes.push_back({ nullptr, wgpu::VertexFormat::Float32x3, 0, POSITION_LOCATION });

			if (program.hasUV)
				vertexAttributes.push_back({ nullptr, wgpu::VertexFormat::Float32x2, format == VertexFormat::Position ? 0 : UV_OFFSET, UV_LOCATION });

			if (program.hasNormal && format == VertexFormat::PositionUVNormal)
				vertexAttributes.push_back({ nullptr, wgpu::VertexFormat::Float32x3, NORMAL_OFFSET, NORMAL_LOCATION });

			const wgpu::VertexAttribute noNormal = { nullptr, wgpu::VertexFormat::Float32x3, 0, NORMAL_LOCATION };

			std::vector<wgpu::VertexAttribute> instanceAttributes;

			for (uint32_t column = 0; column < INSTANCE_COLUMNS; column++)
				instanceAttributes.push_back({ nullptr, wgpu::VertexFormat::Float32x4, column * INSTANCE_COLUMN_SIZE, InstanceModelLocation + column });

			instanceAttributes.push_back({ nullptr, wgpu::VertexFormat::Float32x3, INSTANCE_COLUMNS * INSTANCE_COLUMN_SIZE, InstanceColorLocation });

			if (slots.vertices >= 0)
			{
				wgpu::VertexBufferLayout& layout = buffers.emplace_back();
				layout.arrayStride = FloatsPerVertex(format) * sizeof(float);
				layout.attributeCount = vertexAttributes.size();
				layout.attributes = vertexAttributes.data();
			};

			// No stride, so every vertex of every instance reads the same zeros.
			if (slots.noNormal >= 0)
			{
				wgpu::VertexBufferLayout& layout = buffers.emplace_back();
				layout.arrayStride = 0;
				layout.attributeCount = 1;
				layout.attributes = &noNormal;
			};

			if (slots.instances >= 0)
			{
				wgpu::VertexBufferLayout& layout = buffers.emplace_back();
				layout.stepMode = wgpu::VertexStepMode::Instance;
				layout.arrayStride = INSTANCE_SIZE;
				layout.attributeCount = instanceAttributes.size();
				layout.attributes = instanceAttributes.data();
			};

			// A texture's rows run bottom first, as a GL one's do, so clip space
			// is flipped for it, which turns GL's counter-clockwise front faces
			// clockwise and dFdy around.
			const std::string clipFlipKey = std::to_string(Glsl::CLIP_FLIP_CONSTANT_ID);
			const std::string flipKey = std::to_string(Glsl::FLIP_CONSTANT_ID);
			const wgpu::ConstantEntry clipFlip = { nullptr, clipFlipKey.c_str(), flipped ? FLIPPED : UNFLIPPED };
			const wgpu::ConstantEntry flip = { nullptr, flipKey.c_str(), flipped ? UNFLIPPED : FLIPPED };

			wgpu::RenderPipelineDescriptor descriptor;
			descriptor.layout = program.layout;
			descriptor.vertex.module = program.vertex;
			descriptor.vertex.entryPoint = "main";
			descriptor.vertex.constantCount = program.vertexFlips ? 1 : 0;
			descriptor.vertex.constants = &clipFlip;
			descriptor.vertex.bufferCount = buffers.size();
			descriptor.vertex.buffers = buffers.data();
			descriptor.primitive.topology = topology;
			descriptor.primitive.frontFace = flipped ? wgpu::FrontFace::CW : wgpu::FrontFace::CCW;
			descriptor.primitive.cullMode = wgpu::CullMode::None;

			wgpu::DepthStencilState depth;
			depth.format = DEPTH_FORMAT;
			depth.depthWriteEnabled = wgpu::OptionalBool::True;
			depth.depthCompare = m_depthEqual ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Less;

			// Bias is only allowed where there are faces for it to slope across.
			if (IsTriangles(topology))
			{
				depth.depthBias = (int32_t)std::round(m_biasConstant);
				depth.depthBiasSlopeScale = m_biasSlope;
			};

			if (m_renderingDepth)
				descriptor.depthStencil = &depth;

			wgpu::BlendComponent blend;
			blend.operation = wgpu::BlendOperation::Add;
			blend.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

			wgpu::BlendState blendState;
			blendState.color = blend;
			blendState.alpha = blend;

			wgpu::ColorTargetState color;
			color.format = m_renderingFormat;
			color.blend = &blendState;
			color.writeMask = m_colorWrite ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;

			wgpu::FragmentState fragment;
			fragment.module = program.fragment;
			fragment.entryPoint = "main";
			fragment.constantCount = program.fragmentFlips ? 1 : 0;
			fragment.constants = &flip;
			fragment.targetCount = m_renderingFormat == wgpu::TextureFormat::Undefined ? 0 : 1;
			fragment.targets = &color;
			descriptor.fragment = &fragment;

			// Remembered either way, so a pipeline the device refuses is reported
			// once rather than every frame.
			const wgpu::RenderPipeline pipeline = m_device.CreateRenderPipeline(&descriptor);
			program.pipelines[key] = pipeline;

			return pipeline;
		};

		// Records commands into the frame, between its passes, or outside one
		// runs them at once.
		void Run(const std::function<void(wgpu::CommandEncoder&)>& commands)
		{
			if (!m_recording)
			{
				wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
				commands(encoder);
				Submit(encoder);

				// What was written to the chunks has been copied out by the submit.
				ResetChunks();
				return;
			};

			EndRendering();
			commands(m_encoder);
		};

		void Submit(const wgpu::CommandEncoder& encoder)
		{
			FlushChunks();

			const wgpu::CommandBuffer commands = encoder.Finish();
			m_queue.Submit(1, &commands);
		};

		// Writes size bytes into the words around them, keeping the bytes of
		// those words outside them, since WebGPU copies only whole words.
		void Merge(const wgpu::Buffer& buffer, const void* data, size_t size, size_t offset)
		{
			if (!m_merge)
			{
				wgpu::ShaderSourceWGSL source;
				source.code = MERGE.c_str();

				wgpu::ShaderModuleDescriptor module;
				module.nextInChain = &source;

				wgpu::ComputePipelineDescriptor descriptor;
				descriptor.compute.module = m_device.CreateShaderModule(&module);
				descriptor.compute.entryPoint = "main";
				m_merge = m_device.CreateComputePipeline(&descriptor);
			};

			const uint64_t first = offset / WORD;
			const uint64_t words = RoundUp(offset + size, WORD) / WORD - first;
			const uint64_t incomingSize = (MERGE_HEADER_WORDS + words) * WORD;

			const Allocation incoming = Allocate(incomingSize, m_limits.minStorageBufferOffsetAlignment);
			uint32_t* const header = (uint32_t*)incoming.mapped;
			uint8_t* const bytes = incoming.mapped + MERGE_HEADER_WORDS * WORD;

			header[0] = (uint32_t)offset;
			header[1] = (uint32_t)(offset + size);
			memset(bytes, 0, words * WORD);
			memcpy(bytes + offset - first * WORD, data, size);

			wgpu::BindGroupEntry entries[2];
			entries[0].binding = 0;
			entries[0].buffer = buffer;
			entries[1].binding = 1;
			entries[1].buffer = incoming.buffer;
			entries[1].offset = incoming.offset;
			entries[1].size = incomingSize;

			wgpu::BindGroupDescriptor group;
			group.layout = m_merge.GetBindGroupLayout(0);
			group.entryCount = std::size(entries);
			group.entries = entries;
			const wgpu::BindGroup bound = m_device.CreateBindGroup(&group);

			const uint32_t groups = (uint32_t)std::min<uint64_t>(RoundUp(words, MERGE_WORKGROUP_SIZE) / MERGE_WORKGROUP_SIZE, m_limits.maxComputeWorkgroupsPerDimension);

			Run([&](wgpu::CommandEncoder& encoder)
			{
				const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
				pass.SetPipeline(m_merge);
				pass.SetBindGroup(0, bound);
				pass.DispatchWorkgroups(groups);
				pass.End();
			});
		};

		Allocation Allocate(uint64_t size, uint64_t alignment)
		{
			for (; m_chunk < m_chunks.size(); m_chunk++)
			{
				Chunk& chunk = m_chunks[m_chunk];
				const uint64_t offset = RoundUp(chunk.used, alignment);

				if (offset + size > chunk.written.size())
					continue;

				if (chunk.dirtyStart == chunk.dirtyEnd)
					chunk.dirtyStart = offset;

				chunk.used = offset + size;
				chunk.dirtyEnd = chunk.used;

				return Allocation{ (uint32_t)m_chunk, chunk.buffer, offset, chunk.written.data() + offset };
			};

			Chunk chunk;
			chunk.written.resize(std::max(CHUNK_SIZE, RoundUp(size, WORD)));

			wgpu::BufferDescriptor descriptor;
			descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::Uniform |
				wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
			descriptor.size = chunk.written.size();
			chunk.buffer = m_device.CreateBuffer(&descriptor);

			m_chunks.push_back(std::move(chunk));

			return Allocate(size, alignment);
		};

		void FlushChunks()
		{
			for (Chunk& chunk : m_chunks)
			{
				if (chunk.dirtyStart == chunk.dirtyEnd)
					continue;

				const uint64_t start = chunk.dirtyStart / WORD * WORD;
				const uint64_t end = RoundUp(chunk.dirtyEnd, WORD);

				m_queue.WriteBuffer(chunk.buffer, start, chunk.written.data() + start, end - start);
				chunk.dirtyStart = chunk.dirtyEnd = 0;
			};
		};

		void ResetChunks()
		{
			for (Chunk& chunk : m_chunks)
				chunk.used = 0;

			m_chunk = 0;
		};

		void Write(uint32_t handle, const char* name, const void* data, uint32_t size)
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			Program& program = found->second;
			const auto member = program.members.find(std::string_view(name));

			if (member != program.members.end())
				memcpy(program.block.data() + member->second.offset, data, std::min(size, member->second.size));
		};

		// Moves kept's vertices into a buffer of their own on the GPU, with the
		// first vertex once more after the last, which a loop closes on.
		void Settle(Kept& kept)
		{
			kept.settles = false;

			const size_t stride = FloatsPerVertex(kept.format) * sizeof(float);

			wgpu::BufferDescriptor descriptor;
			descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
			descriptor.size = (kept.count + 1) * stride;
			kept.buffer = m_device.CreateBuffer(&descriptor);

			m_queue.WriteBuffer(kept.buffer, 0, kept.streamed.data(), kept.count * stride);
			m_queue.WriteBuffer(kept.buffer, kept.count * stride, kept.streamed.data(), stride);

			kept.streamed.clear();
			kept.streamed.shrink_to_fit();
		};

		// WebGPU keeps a buffer for as long as a frame still draws from it.
		void Release(Kept& kept)
		{
			kept.buffer = nullptr;
			kept.streamed.clear();
		};

		Readback RecordReadback()
		{
			Readback readback;
			readback.rowBytes = (uint32_t)RoundUp((uint64_t)m_windowSize.x * RGBA, ROW_ALIGNMENT);

			wgpu::BufferDescriptor descriptor;
			descriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
			descriptor.size = (uint64_t)readback.rowBytes * m_windowSize.y;
			readback.buffer = m_device.CreateBuffer(&descriptor);

			wgpu::TexelCopyTextureInfo source;
			source.texture = m_windowTexture;

			wgpu::TexelCopyBufferInfo destination;
			destination.buffer = readback.buffer;
			destination.layout.bytesPerRow = readback.rowBytes;
			destination.layout.rowsPerImage = (uint32_t)m_windowSize.y;

			const wgpu::Extent3D extent = Extent(m_windowSize.x, m_windowSize.y);
			m_encoder.CopyTextureToBuffer(&source, &destination, &extent);

			return readback;
		};

		// Hands the window's pixels over the way GL reads them: red first and
		// bottom row first.
		void DeliverReadback(const Readback& readback)
		{
			if (!m_capture)
				return;

			constexpr int BYTES_PER_PIXEL = 3;
			enum { RED, GREEN, BLUE };

			const CaptureHandler handler = std::move(m_capture);
			m_capture = nullptr;

			std::vector<uint8_t> rgb;

			if (!readback.buffer)
			{
				handler(0, 0, rgb);
				return;
			};

			const uint64_t size = readback.buffer.GetSize();
			Wait(readback.buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) { }));

			const uint8_t* const mapped = (const uint8_t*)readback.buffer.GetConstMappedRange(0, size);
			const int width = m_windowSize.x;
			const int height = m_windowSize.y;
			const bool bgra = m_surfaceFormat == wgpu::TextureFormat::BGRA8Unorm;

			if (mapped)
			{
				rgb.resize((size_t)width * height * BYTES_PER_PIXEL);

				for (int y = 0; y < height; y++)
				{
					const uint8_t* const row = mapped + (size_t)(height - 1 - y) * readback.rowBytes;
					uint8_t* const out = rgb.data() + (size_t)y * width * BYTES_PER_PIXEL;

					for (int x = 0; x < width; x++)
					{
						const uint8_t* const pixel = row + (size_t)x * RGBA;
						out[x * BYTES_PER_PIXEL + RED] = pixel[bgra ? BLUE : RED];
						out[x * BYTES_PER_PIXEL + GREEN] = pixel[GREEN];
						out[x * BYTES_PER_PIXEL + BLUE] = pixel[bgra ? RED : BLUE];
					};
				};
			};

			readback.buffer.Unmap();
			handler(mapped ? width : 0, mapped ? height : 0, rgb);
		};

		void DeliverReads()
		{
			std::vector<Read> reads = std::move(m_reads);
			m_reads.clear();

			for (const Read& read : reads)
			{
				const uint64_t size = read.buffer.GetSize();
				Wait(read.buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) { }));

				const uint8_t* const mapped = (const uint8_t*)read.buffer.GetConstMappedRange(0, size);
				const std::vector<uint8_t> data = mapped ? std::vector<uint8_t>(mapped, mapped + read.size) : std::vector<uint8_t>();

				read.buffer.Unmap();
				read.handler(data);
			};
		};

		wgpu::Instance m_instance;
		wgpu::Adapter m_adapter;
		wgpu::Device m_device;
		wgpu::Queue m_queue;
		wgpu::Surface m_surface;
		wgpu::Limits m_limits;
		wgpu::TextureFormat m_surfaceFormat = wgpu::TextureFormat::Undefined;

		wgpu::Texture m_windowTexture;
		wgpu::TextureView m_windowColor;
		wgpu::TextureView m_windowDepth;
		glm::ivec2 m_windowSize{ };
		bool m_haveWindow = false;
		Pending m_windowPending;

		wgpu::CommandEncoder m_encoder;
		wgpu::RenderPassEncoder m_pass;
		bool m_recording = false;

		// What is being drawn into, the window at the bottom.
		std::vector<uint32_t> m_stack = { WINDOW };

		bool m_rendering = false;
		uint32_t m_renderingTarget = WINDOW;
		bool m_renderingDepth = false;
		wgpu::TextureFormat m_renderingFormat = wgpu::TextureFormat::Undefined;

		float m_biasSlope = 0.0f;
		float m_biasConstant = 0.0f;
		bool m_colorWrite = true;
		bool m_depthEqual = false;

		CaptureHandler m_capture;
		bool m_imgui = false;
		std::vector<wgpu::TextureView> m_imguiViews;

		std::vector<Chunk> m_chunks;
		size_t m_chunk = 0;

		wgpu::Sampler m_linear;
		wgpu::Sampler m_nearest;
		wgpu::Sampler m_shadow;
		wgpu::Buffer m_zeros;
		wgpu::ComputePipeline m_merge;

		uint32_t m_nextHandle = 1;
		std::unordered_map<uint32_t, Program> m_programs;
		std::unordered_map<uint32_t, Texture> m_textures;
		std::unordered_map<uint32_t, StorageBuffer> m_buffers;
		std::vector<Read> m_reads;
		std::unordered_map<uint32_t, Kept> m_kept;
		uint32_t m_nextKept = 1;
		unsigned long long m_framesBegun = 0;
		uint32_t m_fallbackColor = 0;
		uint32_t m_fallbackDepth = 0;
	};

	std::unique_ptr<Renderer> CreateWebGPURenderer()
	{
		return std::make_unique<WebGPURenderer>();
	};
};

#endif

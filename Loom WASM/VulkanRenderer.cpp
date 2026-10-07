#ifndef __EMSCRIPTEN__

#include "Renderer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include "spirv_reflect.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr uint32_t API_VERSION = VK_API_VERSION_1_3;
		constexpr uint32_t FRAMES_IN_FLIGHT = 2;

		// Vertex and fragment.
		constexpr size_t STAGES = 2;

		constexpr size_t RGBA = 4;

		// Where each part of a pipeline's key sits: the topology in the low
		// byte, then the colour format, then whether there is depth, then
		// whether the viewport is flipped.
		constexpr int FORMAT_SHIFT = 8;
		constexpr int DEPTH_SHIFT = 40;
		constexpr int FLIP_SHIFT = 41;

		constexpr VkFormat COLOR_TARGET_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
		constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

		// Vertices and uniform blocks are written into host-visible chunks of
		// this size, reused once their frame has finished on the GPU.
		constexpr VkDeviceSize CHUNK_SIZE = 4 * 1024 * 1024;

		// Loose uniforms are gathered into one block, at this binding.
		constexpr uint32_t DEFAULT_BLOCK_BINDING = 0;

		// The fragment stage's specialisation constant that undoes the window's
		// flipped viewport for dFdy (see FRAGMENT_PRELUDE).
		constexpr uint32_t FLIP_CONSTANT_ID = 0;

		constexpr uint32_t IMGUI_DESCRIPTORS = 1024;
		constexpr int GLSL_VERSION = 460;

		// The Vulkan GLSL semantics glslang compiles to (the VULKAN macro's value).
		constexpr int VULKAN_INPUT_VERSION = 100;

		constexpr uint32_t POSITION_LOCATION = 0;
		constexpr uint32_t POSITION_COMPONENTS = 3;
		constexpr uint32_t POSITION_STRIDE = POSITION_COMPONENTS * sizeof(float);

		constexpr float FAR_DEPTH = 1.0f;
		constexpr uint8_t WHITE = 0xFF;

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

		const char* const VERSION_LINE = "#version 460 core\n";

		// The window is drawn with a flipped viewport, so its rows come out top
		// first while clip space keeps GL's y up. That reverses screen space y,
		// which dFdy would show; this puts it back. #line keeps the compiler's
		// line numbers where GL's would be.
		const char* const FRAGMENT_PRELUDE =
			"layout(constant_id = 0) const float loom_FlipY = 1.0;\n"
			"#define dFdy(p) (loom_FlipY * dFdy(p))\n"
			"#define dFdyFine(p) (loom_FlipY * dFdyFine(p))\n"
			"#define dFdyCoarse(p) (loom_FlipY * dFdyCoarse(p))\n"
			"#line 2\n";

		// Looked up by the const char* a caller has, without building a string.
		struct NameHash
		{
			using is_transparent = void;

			size_t operator()(std::string_view name) const { return std::hash<std::string_view>{ }(name); };
		};

		template<typename Value>
		using ByName = std::unordered_map<std::string, Value, NameHash, std::equal_to<>>;

		VkPrimitiveTopology Topology(uint32_t primitive)
		{
			switch (primitive)
			{
			case GL_POINTS_:			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case GL_LINES_:				return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			// Vulkan has no loop: Draw closes it into a strip.
			case GL_LINE_LOOP_:
			case GL_LINE_STRIP_:		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case GL_TRIANGLE_STRIP_:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case GL_TRIANGLE_FAN_:		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			default:					return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			};
		};

		bool Succeeded(VkResult result, const char* what)
		{
			if (result == VK_SUCCESS)
				return true;

			std::cerr << "Vulkan: " << what << " failed (" << result << ')' << std::endl;
			return false;
		};

		VKAPI_ATTR VkBool32 VKAPI_CALL OnDebugMessage(
			VkDebugUtilsMessageSeverityFlagBitsEXT,
			VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void*)
		{
			std::cerr << "Vulkan Debug: " << data->pMessage << std::endl;
			return VK_FALSE;
		};

#ifndef NDEBUG
		bool HasLayer(const char* name)
		{
			uint32_t count = 0;
			vkEnumerateInstanceLayerProperties(&count, nullptr);

			std::vector<VkLayerProperties> layers(count);
			vkEnumerateInstanceLayerProperties(&count, layers.data());

			return std::any_of(layers.begin(), layers.end(),
				[name](const VkLayerProperties& layer) { return strcmp(layer.layerName, name) == 0; });
		};
#endif

		bool HasExtension(VkPhysicalDevice device, const char* name)
		{
			uint32_t count = 0;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);

			std::vector<VkExtensionProperties> extensions(count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

			return std::any_of(extensions.begin(), extensions.end(),
				[name](const VkExtensionProperties& extension) { return strcmp(extension.extensionName, name) == 0; });
		};

		const std::array<const char*, 4> DEVICE_EXTENSIONS =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
			// Core in 1.3, but ImGui's backend looks it up by its KHR names to
			// draw the windows dragged out of the main one.
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			// GL's -1 to 1 clip space depth, so shaders and projections carry over.
			VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME,
		};

		// Compiles both stages with GL's rules relaxed onto Vulkan's: loose
		// uniforms are gathered into one block, and bindings and locations are
		// given out across the two stages together.
		std::array<std::vector<uint32_t>, STAGES> CompileToSpirv(const std::string& name, const std::string& vertex, const std::string& fragment)
		{
			const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

			const std::string sources[] =
			{
				VERSION_LINE + vertex,
				VERSION_LINE + std::string(FRAGMENT_PRELUDE) + fragment,
			};

			const EShLanguage stages[] = { EShLangVertex, EShLangFragment };
			const char* const stageNames[] = { "VERTEX", "FRAGMENT" };

			glslang::TShader vertexShader(EShLangVertex);
			glslang::TShader fragmentShader(EShLangFragment);
			glslang::TShader* shaders[] = { &vertexShader, &fragmentShader };

			glslang::TProgram program;

			for (size_t i = 0; i < std::size(shaders); i++)
			{
				glslang::TShader& shader = *shaders[i];
				const char* text = sources[i].c_str();

				shader.setStrings(&text, 1);
				shader.setEnvInput(glslang::EShSourceGlsl, stages[i], glslang::EShClientVulkan, VULKAN_INPUT_VERSION);
				shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
				shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
				shader.setEnvInputVulkanRulesRelaxed();
				shader.setAutoMapBindings(true);
				shader.setAutoMapLocations(true);
				shader.setGlobalUniformSet(0);
				shader.setGlobalUniformBinding(DEFAULT_BLOCK_BINDING);

				if (!shader.parse(GetDefaultResources(), GLSL_VERSION, false, messages))
					throw std::runtime_error(std::string("Shader compile error in ") + stageNames[i] + ":\n" + shader.getInfoLog());

				program.addShader(&shader);
			};

			if (!program.link(messages) || !program.mapIO())
				throw std::runtime_error("Shader link error in " + name + ":\n" + program.getInfoLog());

			std::array<std::vector<uint32_t>, STAGES> spirv;

			for (size_t i = 0; i < std::size(stages); i++)
				glslang::GlslangToSpv(*program.getIntermediate(stages[i]), spirv[i]);

			return spirv;
		};
	};

	struct VulkanRenderer final : Renderer
	{
		VulkanRenderer()
		{
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		};

		~VulkanRenderer() override
		{
			if (m_device)
			{
				vkDeviceWaitIdle(m_device);

				for (Frame& frame : m_frames)
					RunDeletions(frame);

				for (auto& [handle, program] : m_programs)
					DestroyProgram(program);

				for (auto& [handle, texture] : m_textures)
					DestroyTextureNow(texture);

				for (Frame& frame : m_frames)
				{
					for (Chunk& chunk : frame.chunks)
						DestroyBuffer(chunk.buffer, chunk.memory);

					vkDestroyFence(m_device, frame.fence, nullptr);
					vkDestroySemaphore(m_device, frame.acquired, nullptr);
					vkDestroyCommandPool(m_device, frame.pool, nullptr);
				};

				DestroySwapchain();

				vkDestroyCommandPool(m_device, m_immediatePool, nullptr);
				vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
				vkDestroyDevice(m_device, nullptr);
			};

			if (m_surface)		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			if (m_messenger)	DestroyMessenger();
			if (m_instance)		vkDestroyInstance(m_instance, nullptr);

			if (m_glslang)
				glslang::FinalizeProcess();
		};

		Backend GetBackend() const override
		{
			return Backend::Vulkan;
		};

		bool Attach(GLFWwindow* window) override
		{
			m_window = window;

			// Asked before any Vulkan call, so a machine without a Vulkan driver
			// never needs the loader the engine is linked against.
			if (!glfwVulkanSupported())
			{
				std::cerr << "Vulkan is not available on this machine" << std::endl;
				return false;
			};

			if (!CreateInstance()
				|| !Succeeded(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface), "creating the window surface")
				|| !PickDevice()
				|| !CreateDevice()
				|| !CreateFrames()
				|| !CreateSwapchain())
				return false;

			m_glslang = glslang::InitializeProcess();

			glfwSetFramebufferSizeCallback(
				window,
				[](GLFWwindow*, int, int)
				{
					if (VulkanRenderer* renderer = (VulkanRenderer*)Renderer::Get())
						renderer->m_swapchainDirty = true;
				});

			const uint8_t white[] = { WHITE, WHITE, WHITE, WHITE };
			m_fallbackColor = CreateTexture(1, 1, white);
			m_fallbackDepth = CreateDepthTarget(1);

			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
			m_uniformAlignment = properties.limits.minUniformBufferOffsetAlignment;
			m_maxTextureSize = (int)properties.limits.maxImageDimension2D;

			std::cout << "Vulkan Version: "
				<< VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
				<< VK_API_VERSION_MINOR(properties.apiVersion) << '.'
				<< VK_API_VERSION_PATCH(properties.apiVersion) << std::endl;
			std::cout << "Vulkan Device: " << properties.deviceName << std::endl;

			return m_fallbackColor && m_fallbackDepth;
		};

		void InitImGui(GLFWwindow* window) override
		{
			const VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_DESCRIPTORS };

			VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			pool.maxSets = IMGUI_DESCRIPTORS;
			pool.poolSizeCount = 1;
			pool.pPoolSizes = &size;
			Succeeded(vkCreateDescriptorPool(m_device, &pool, nullptr, &m_imguiPool), "creating ImGui's descriptor pool");

			ImGui_ImplGlfw_InitForVulkan(window, true);

			ImGui_ImplVulkan_InitInfo info{ };
			info.Instance = m_instance;
			info.PhysicalDevice = m_physicalDevice;
			info.Device = m_device;
			info.QueueFamily = m_queueFamily;
			info.Queue = m_queue;
			info.DescriptorPool = m_imguiPool;
			info.MinImageCount = m_minImageCount;
			info.ImageCount = (uint32_t)m_swapImages.size();
			info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

			// ImGui draws in a pass of its own over the window's colour, with no
			// depth, which is also all a window dragged out of this one has.
			info.UseDynamicRendering = true;
			info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
			info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
			info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_swapFormat;

			info.CheckVkResultFn = [](VkResult result) { Succeeded(result, "ImGui"); };

			ImGui_ImplVulkan_Init(&info);
			m_imgui = true;
		};

		void ShutdownImGui() override
		{
			vkDeviceWaitIdle(m_device);

			for (auto& [handle, texture] : m_textures)
				texture.imgui = VK_NULL_HANDLE;

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			m_imgui = false;
		};

		void NewImGuiFrame() override
		{
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
		};

		void RenderImGui(ImDrawData* drawData) override
		{
			if (!m_recording || !m_haveImage)
				return;

			EndRendering();
			BeginRendering(WINDOW, false);
			ImGui_ImplVulkan_RenderDrawData(drawData, Command());
			EndRendering();
		};

		void RenderImGuiWindows() override
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		};

		void ReleaseImGuiFonts() override
		{
			// The frames in flight may still be drawing with it.
			vkDeviceWaitIdle(m_device);
			ImGui_ImplVulkan_DestroyFontsTexture();
		};

		void BeginFrame() override
		{
			Frame& frame = m_frames[m_frameIndex];

			vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
			RunDeletions(frame);
			frame.chunkIndex = 0;

			for (Chunk& chunk : frame.chunks)
				chunk.used = 0;

			if (m_swapchainDirty)
				CreateSwapchain();

			m_haveImage = Acquire(frame);

			vkResetFences(m_device, 1, &frame.fence);
			vkResetCommandPool(m_device, frame.pool, 0);

			VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(frame.command, &begin);

			m_recording = true;
			m_rendering = false;
			m_stack.assign(1, WINDOW);
			m_windowPending = Pending{ };

			if (m_haveImage)
				m_swapLayouts[m_imageIndex] = VK_IMAGE_LAYOUT_UNDEFINED;
		};

		void EndFrame() override
		{
			if (!m_recording)
				return;

			Frame& frame = m_frames[m_frameIndex];
			const VkCommandBuffer command = frame.command;

			EndRendering();

			Readback readback;

			if (m_haveImage)
			{
				// A clear nothing was drawn after still has to land.
				if (m_windowPending.color || m_windowPending.depth || m_swapLayouts[m_imageIndex] == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					m_stack.assign(1, WINDOW);
					BeginRendering(WINDOW, true);
					EndRendering();
				};

				if (m_capture)
					readback = RecordReadback(command);

				Transition(command, m_swapImages[m_imageIndex], VK_IMAGE_ASPECT_COLOR_BIT, m_swapLayouts[m_imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
				m_swapLayouts[m_imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			};

			vkEndCommandBuffer(command);
			m_recording = false;

			const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

			VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &command;

			if (m_haveImage)
			{
				submit.waitSemaphoreCount = 1;
				submit.pWaitSemaphores = &frame.acquired;
				submit.pWaitDstStageMask = &wait;
				submit.signalSemaphoreCount = 1;
				submit.pSignalSemaphores = &m_rendered[m_imageIndex];
			};

			Succeeded(vkQueueSubmit(m_queue, 1, &submit, frame.fence), "submitting the frame");

			if (m_haveImage)
			{
				VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
				present.waitSemaphoreCount = 1;
				present.pWaitSemaphores = &m_rendered[m_imageIndex];
				present.swapchainCount = 1;
				present.pSwapchains = &m_swapchain;
				present.pImageIndices = &m_imageIndex;

				const VkResult result = vkQueuePresentKHR(m_queue, &present);

				if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
					m_swapchainDirty = true;
			};

			if (m_capture)
			{
				vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
				DeliverReadback(readback);
			};

			m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
		};

		void Capture(const CaptureHandler& handler) override
		{
			m_capture = handler;
		};

		uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) override
		{
			const std::array<std::vector<uint32_t>, STAGES> spirv = CompileToSpirv(name, vertex, fragment);

			Program program;
			program.name = name;

			for (const std::vector<uint32_t>& code : spirv)
				Reflect(program, code);

			for (size_t i = 0; i < spirv.size(); i++)
			{
				VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
				info.codeSize = spirv[i].size() * sizeof(uint32_t);
				info.pCode = spirv[i].data();
				vkCreateShaderModule(m_device, &info, nullptr, &program.modules[i]);
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

			if (program.blockSize)
				bindings.push_back({ DEFAULT_BLOCK_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr });

			for (const auto& [samplerName, sampler] : program.samplers)
				bindings.push_back({ sampler.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr });

			VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			set.bindingCount = (uint32_t)bindings.size();
			set.pBindings = bindings.data();
			vkCreateDescriptorSetLayout(m_device, &set, nullptr, &program.setLayout);

			VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layout.setLayoutCount = 1;
			layout.pSetLayouts = &program.setLayout;
			vkCreatePipelineLayout(m_device, &layout, nullptr, &program.layout);

			program.block.assign(program.blockSize, 0);

			const uint32_t handle = m_nextHandle++;
			m_programs.emplace(handle, std::move(program));

			return handle;
		};

		void DeleteProgram(uint32_t handle) override
		{
			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			Defer([this, program = std::move(found->second)]() mutable { DestroyProgram(program); });
			m_programs.erase(found);
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

		void SetTexture(uint32_t program, const char* name, uint32_t texture) override
		{
			const auto found = m_programs.find(program);

			if (found == m_programs.end())
				return;

			const auto sampler = found->second.samplers.find(std::string_view(name));

			if (sampler != found->second.samplers.end())
				sampler->second.texture = texture;
		};

		void Draw(uint32_t handle, uint32_t primitive, const float* positions, size_t vertices, uint32_t) override
		{
			if (!m_recording || vertices == 0)
				return;

			const auto found = m_programs.find(handle);

			if (found == m_programs.end())
				return;

			Program& program = found->second;
			const uint32_t target = m_stack.back();

			if (target == WINDOW && !m_haveImage)
				return;

			EnsureRendering(target);

			const VkPipeline pipeline = PipelineFor(program, Topology(primitive), target);

			if (!pipeline)
				return;

			const VkCommandBuffer command = Command();

			if (pipeline != m_boundPipeline)
			{
				vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				m_boundPipeline = pipeline;
			};

			// A loop is a strip with its first vertex again at the end.
			const size_t drawn = primitive == GL_LINE_LOOP_ ? vertices + 1 : vertices;

			if (program.hasPosition)
			{
				const Allocation vertexData = Allocate(drawn * POSITION_STRIDE, sizeof(float));
				memcpy(vertexData.mapped, positions, vertices * POSITION_STRIDE);

				if (drawn > vertices)
					memcpy(vertexData.mapped + vertices * POSITION_STRIDE, positions, POSITION_STRIDE);

				vkCmdBindVertexBuffers(command, 0, 1, &vertexData.buffer, &vertexData.offset);
			};

			// Members, so a draw allocates nothing; reserved, so the writes'
			// pointers into images stay put.
			std::vector<VkWriteDescriptorSet>& writes = m_writes;
			std::vector<VkDescriptorImageInfo>& images = m_images;
			writes.clear();
			images.clear();
			images.reserve(program.samplers.size());

			VkDescriptorBufferInfo block{ };

			if (program.blockSize)
			{
				const Allocation uniforms = Allocate(program.blockSize, m_uniformAlignment);
				memcpy(uniforms.mapped, program.block.data(), program.blockSize);

				block = { uniforms.buffer, uniforms.offset, program.blockSize };

				VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstBinding = DEFAULT_BLOCK_BINDING;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				write.pBufferInfo = &block;
				writes.push_back(write);
			};

			for (const auto& [name, sampler] : program.samplers)
			{
				const Texture& texture = SampledTexture(sampler);
				images.push_back({ texture.sampler, texture.Sampled().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

				VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstBinding = sampler.binding;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.pImageInfo = &images.back();
				writes.push_back(write);
			};

			if (!writes.empty())
				m_pushDescriptorSet(command, VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, (uint32_t)writes.size(), writes.data());

			vkCmdDraw(command, (uint32_t)drawn, 1, 0, 0);
		};

		uint32_t CreateTexture(int width, int height, const uint8_t* rgba) override
		{
			Texture texture;
			texture.width = width;
			texture.height = height;

			if (!CreateAttachment(texture.color, COLOR_TARGET_FORMAT, width, height,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
				return 0;

			texture.sampler = CreateSampler(false);

			const VkDeviceSize size = (VkDeviceSize)width * height * sizeof(uint32_t);
			VkBuffer staging = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			void* mapped = nullptr;

			if (rgba && CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, memory, &mapped))
				memcpy(mapped, rgba, size);

			Immediately([&](VkCommandBuffer command)
			{
				Transition(command, texture.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

				if (staging)
				{
					VkBufferImageCopy copy{ };
					copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
					copy.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
					vkCmdCopyBufferToImage(command, staging, texture.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
				};

				Transition(command, texture.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			});

			DestroyBuffer(staging, memory);

			return Add(std::move(texture));
		};

		uint32_t CreateDepthTarget(int size) override
		{
			Texture texture;
			texture.width = texture.height = size;

			if (!CreateAttachment(texture.depth, DEPTH_FORMAT, size, size,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_IMAGE_ASPECT_DEPTH_BIT))
			{
				std::cerr << "A " << size << " square depth target could not be allocated" << std::endl;
				return 0;
			};

			texture.sampler = CreateSampler(true);

			// Starts at the far plane, so a map nothing has been drawn into yet
			// puts nothing in shadow.
			Immediately([&](VkCommandBuffer command)
			{
				Transition(command, texture.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

				const VkClearDepthStencilValue far = { FAR_DEPTH, 0 };
				const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
				vkCmdClearDepthStencilImage(command, texture.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &range);

				Transition(command, texture.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			});

			texture.depth.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			return Add(std::move(texture));
		};

		uint32_t CreateColorTarget(int width, int height) override
		{
			Texture texture;
			texture.width = width;
			texture.height = height;

			if (!CreateAttachment(texture.color, COLOR_TARGET_FORMAT, width, height,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
				|| !CreateAttachment(texture.depth, DEPTH_FORMAT, width, height,
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT))
			{
				std::cerr << "A " << width << 'x' << height << " render target could not be allocated" << std::endl;
				DestroyTextureNow(texture);
				return 0;
			};

			texture.sampler = CreateSampler(false);

			Immediately([&](VkCommandBuffer command)
			{
				Transition(command, texture.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				Transition(command, texture.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
			});

			texture.color.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			texture.depth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

			return Add(std::move(texture));
		};

		void DestroyTexture(uint32_t handle) override
		{
			const auto found = m_textures.find(handle);

			if (found == m_textures.end())
				return;

			Defer([this, texture = found->second]() mutable { DestroyTextureNow(texture); });
			m_textures.erase(found);
		};

		void PushTarget(uint32_t target) override
		{
			EndRendering();
			m_stack.push_back(m_textures.contains(target) ? target : WINDOW);
		};

		void PopTarget() override
		{
			if (m_stack.size() <= 1)
				return;

			// A clear with nothing drawn after it still has to land.
			const Pending* pending = PendingOf(m_stack.back());

			if (m_recording && pending && (pending->color || pending->depth))
				EnsureRendering(m_stack.back());

			EndRendering();
			m_stack.pop_back();
		};

		void Clear(const float* color, bool depth) override
		{
			const uint32_t target = m_stack.back();
			Pending* pending = PendingOf(target);

			if (!pending)
				return;

			if (color)
			{
				pending->color = true;
				std::copy(color, color + std::size(pending->rgba), pending->rgba);
			};

			pending->depth |= depth;

			// Mid-pass, the clear has to be drawn rather than loaded.
			if (m_rendering && m_renderingTarget == target)
				ClearInPass(target);
		};

		void SetDepthBias(float slope, float constant) override
		{
			m_biasSlope = slope;
			m_biasConstant = constant;

			if (m_rendering)
				vkCmdSetDepthBias(Command(), m_biasConstant, 0.0f, m_biasSlope);
		};

		glm::ivec2 TargetSize() const override
		{
			const uint32_t target = m_stack.back();

			if (target == WINDOW)
				return glm::ivec2((int)m_swapExtent.width, (int)m_swapExtent.height);

			const Texture& texture = m_textures.at(target);

			return glm::ivec2(texture.width, texture.height);
		};

		int MaxTextureSize() const override
		{
			return m_maxTextureSize;
		};

		void* ImGuiTexture(uint32_t handle) override
		{
			const auto found = m_textures.find(handle);

			if (!m_imgui || found == m_textures.end())
				return nullptr;

			Texture& texture = found->second;

			if (!texture.imgui)
				texture.imgui = ImGui_ImplVulkan_AddTexture(texture.sampler, texture.Sampled().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			return (void*)texture.imgui;
		};

	private:
		// The window, as a target.
		static constexpr uint32_t WINDOW = 0;

		struct Attachment
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkFormat format = VK_FORMAT_UNDEFINED;
		};

		// A clear asked for before anything was drawn, done by the pass's load.
		struct Pending
		{
			bool color = false;
			bool depth = false;
			float rgba[RGBA]{ };
		};

		// A plain texture has only colour, a depth target only depth, and a
		// colour target both. Whichever is there first is what a shader samples.
		struct Texture
		{
			Attachment color;
			Attachment depth;
			VkSampler sampler = VK_NULL_HANDLE;
			int width = 0;
			int height = 0;
			Pending pending;
			VkDescriptorSet imgui = VK_NULL_HANDLE;

			const Attachment& Sampled() const { return color.image ? color : depth; };
		};

		struct Program
		{
			struct Member
			{
				uint32_t offset;
				uint32_t size;
			};

			struct Sampler
			{
				uint32_t binding;
				bool shadow;
				uint32_t texture = 0;
			};

			std::string name;
			std::array<VkShaderModule, STAGES> modules{ };
			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkPipelineLayout layout = VK_NULL_HANDLE;

			bool hasPosition = false;
			uint32_t blockSize = 0;
			std::vector<uint8_t> block;
			ByName<Member> members;
			ByName<Sampler> samplers;

			// By topology, attachment formats and whether the viewport is flipped.
			std::unordered_map<uint64_t, VkPipeline> pipelines;
		};

		struct Chunk
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			uint8_t* mapped = nullptr;
			VkDeviceSize size = 0;
			VkDeviceSize used = 0;
		};

		struct Allocation
		{
			VkBuffer buffer;
			VkDeviceSize offset;
			uint8_t* mapped;
		};

		struct Frame
		{
			VkCommandPool pool = VK_NULL_HANDLE;
			VkCommandBuffer command = VK_NULL_HANDLE;
			VkFence fence = VK_NULL_HANDLE;
			VkSemaphore acquired = VK_NULL_HANDLE;

			std::vector<Chunk> chunks;
			size_t chunkIndex = 0;

			// Run once the GPU is done with this frame.
			std::vector<std::function<void()>> deletions;
		};

		struct Readback
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			uint8_t* mapped = nullptr;
			int width = 0;
			int height = 0;
		};

		bool CreateInstance()
		{
			uint32_t count = 0;
			const char** required = glfwGetRequiredInstanceExtensions(&count);
			std::vector<const char*> extensions(required, required + count);
			std::vector<const char*> layers;

#ifndef NDEBUG
			constexpr const char* VALIDATION = "VK_LAYER_KHRONOS_validation";

			if (HasLayer(VALIDATION))
			{
				layers.push_back(VALIDATION);
				extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			};
#endif

			VkApplicationInfo application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			application.pApplicationName = "Loom";
			application.pEngineName = "Loom";
			application.apiVersion = API_VERSION;

			VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			info.pApplicationInfo = &application;
			info.enabledExtensionCount = (uint32_t)extensions.size();
			info.ppEnabledExtensionNames = extensions.data();
			info.enabledLayerCount = (uint32_t)layers.size();
			info.ppEnabledLayerNames = layers.data();

			if (!Succeeded(vkCreateInstance(&info, nullptr, &m_instance), "creating the instance"))
				return false;

			if (!layers.empty())
				CreateMessenger();

			return true;
		};

		void CreateMessenger()
		{
			auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");

			if (!create)
				return;

			VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
			info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			info.pfnUserCallback = OnDebugMessage;

			create(m_instance, &info, nullptr, &m_messenger);
		};

		void DestroyMessenger()
		{
			auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");

			if (destroy)
				destroy(m_instance, m_messenger, nullptr);
		};

		// The first device that can do everything this needs, a discrete one
		// over any other.
		bool PickDevice()
		{
			uint32_t count = 0;
			vkEnumeratePhysicalDevices(m_instance, &count, nullptr);

			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

			for (const bool discrete : { true, false })
				for (VkPhysicalDevice device : devices)
				{
					VkPhysicalDeviceProperties properties;
					vkGetPhysicalDeviceProperties(device, &properties);

					if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) != discrete
						|| properties.apiVersion < API_VERSION
						|| !std::all_of(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end(),
							[device](const char* name) { return HasExtension(device, name); }))
						continue;

					uint32_t families = 0;
					vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);

					std::vector<VkQueueFamilyProperties> queues(families);
					vkGetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data());

					for (uint32_t family = 0; family < families; family++)
					{
						VkBool32 present = VK_FALSE;
						vkGetPhysicalDeviceSurfaceSupportKHR(device, family, m_surface, &present);

						if (present && (queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
						{
							m_physicalDevice = device;
							m_queueFamily = family;
							return true;
						};
					};
				};

			std::cerr << "Vulkan: no device has the 1.3 features, push descriptors and GL style depth this needs" << std::endl;
			return false;
		};

		bool CreateDevice()
		{
			const float priority = 1.0f;

			VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queue.queueFamilyIndex = m_queueFamily;
			queue.queueCount = 1;
			queue.pQueuePriorities = &priority;

			VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT };
			depthClip.depthClipControl = VK_TRUE;

			std::vector<const char*> extensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());

			// Lets a shader that draws points leave gl_PointSize alone, as GL
			// does. Without it such points are a size the driver picks.
			VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR };
			maintenance5.maintenance5 = VK_TRUE;

			if (HasExtension(m_physicalDevice, VK_KHR_MAINTENANCE_5_EXTENSION_NAME))
			{
				extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
				depthClip.pNext = &maintenance5;
			};

			VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			features13.dynamicRendering = VK_TRUE;
			features13.pNext = &depthClip;

			VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			info.pNext = &features13;
			info.queueCreateInfoCount = 1;
			info.pQueueCreateInfos = &queue;
			info.enabledExtensionCount = (uint32_t)extensions.size();
			info.ppEnabledExtensionNames = extensions.data();

			if (!Succeeded(vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device), "creating the device"))
				return false;

			vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

			// Through the device rather than the loader's exports, which an older
			// loader than these headers may not have.
			m_pushDescriptorSet = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(m_device, "vkCmdPushDescriptorSetKHR");
			m_beginRendering = (PFN_vkCmdBeginRendering)vkGetDeviceProcAddr(m_device, "vkCmdBeginRendering");
			m_endRendering = (PFN_vkCmdEndRendering)vkGetDeviceProcAddr(m_device, "vkCmdEndRendering");

			return m_pushDescriptorSet && m_beginRendering && m_endRendering;
		};

		bool CreateFrames()
		{
			VkCommandPoolCreateInfo pool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			pool.queueFamilyIndex = m_queueFamily;
			pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

			if (!Succeeded(vkCreateCommandPool(m_device, &pool, nullptr, &m_immediatePool), "creating a command pool"))
				return false;

			for (Frame& frame : m_frames)
			{
				if (!Succeeded(vkCreateCommandPool(m_device, &pool, nullptr, &frame.pool), "creating a command pool"))
					return false;

				VkCommandBufferAllocateInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
				command.commandPool = frame.pool;
				command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				command.commandBufferCount = 1;
				vkAllocateCommandBuffers(m_device, &command, &frame.command);

				VkFenceCreateInfo fence{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
				fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				vkCreateFence(m_device, &fence, nullptr, &frame.fence);

				VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
				vkCreateSemaphore(m_device, &semaphore, nullptr, &frame.acquired);
			};

			return true;
		};

		bool CreateSwapchain()
		{
			m_swapchainDirty = false;

			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(m_window, &width, &height);

			if (m_device)
				vkDeviceWaitIdle(m_device);

			VkSurfaceCapabilitiesKHR capabilities;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

			// Minimised: there is nothing to present into until it comes back.
			if (width <= 0 || height <= 0 || capabilities.maxImageExtent.width == 0)
			{
				m_swapExtent = { 0, 0 };
				return true;
			};

			if (m_swapFormat == VK_FORMAT_UNDEFINED)
				m_swapFormat = PickSurfaceFormat();

			m_swapExtent = capabilities.currentExtent.width != UINT32_MAX
				? capabilities.currentExtent
				: VkExtent2D{
					std::clamp((uint32_t)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
					std::clamp((uint32_t)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };

			m_minImageCount = capabilities.minImageCount + 1;

			if (capabilities.maxImageCount)
				m_minImageCount = std::min(m_minImageCount, capabilities.maxImageCount);

			VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
			info.surface = m_surface;
			info.minImageCount = m_minImageCount;
			info.imageFormat = m_swapFormat;
			info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			info.imageExtent = m_swapExtent;
			info.imageArrayLayers = 1;
			info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.preTransform = capabilities.currentTransform;
			info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
			info.clipped = VK_TRUE;
			info.oldSwapchain = m_swapchain;

			VkSwapchainKHR swapchain = VK_NULL_HANDLE;

			if (!Succeeded(vkCreateSwapchainKHR(m_device, &info, nullptr, &swapchain), "creating the swapchain"))
				return false;

			DestroySwapchain();
			m_swapchain = swapchain;

			uint32_t count = 0;
			vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
			m_swapImages.resize(count);
			vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapImages.data());

			m_swapLayouts.assign(count, VK_IMAGE_LAYOUT_UNDEFINED);
			m_swapViews.resize(count);
			m_rendered.resize(count);

			for (uint32_t i = 0; i < count; i++)
			{
				m_swapViews[i] = CreateView(m_swapImages[i], m_swapFormat, VK_IMAGE_ASPECT_COLOR_BIT);

				VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
				vkCreateSemaphore(m_device, &semaphore, nullptr, &m_rendered[i]);
			};

			return CreateAttachment(m_windowDepth, DEPTH_FORMAT, (int)m_swapExtent.width, (int)m_swapExtent.height,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
		};

		VkFormat PickSurfaceFormat() const
		{
			uint32_t count = 0;
			vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr);

			std::vector<VkSurfaceFormatKHR> formats(count);
			vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, formats.data());

			// Not sRGB: GL's default framebuffer writes what the shader says.
			for (const VkFormat wanted : { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM })
				for (const VkSurfaceFormatKHR& format : formats)
					if (format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
						return wanted;

			return formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats.front().format;
		};

		void DestroySwapchain()
		{
			for (VkImageView view : m_swapViews)
				vkDestroyImageView(m_device, view, nullptr);

			for (VkSemaphore semaphore : m_rendered)
				vkDestroySemaphore(m_device, semaphore, nullptr);

			m_swapViews.clear();
			m_rendered.clear();
			m_swapImages.clear();
			m_swapLayouts.clear();

			DestroyAttachment(m_windowDepth);

			if (m_swapchain)
				vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

			m_swapchain = VK_NULL_HANDLE;
		};

		bool Acquire(Frame& frame)
		{
			if (!m_swapchain || m_swapExtent.width == 0)
			{
				m_swapchainDirty = true;
				return false;
			};

			VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &m_imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				CreateSwapchain();

				if (!m_swapchain || m_swapExtent.width == 0)
					return false;

				result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &m_imageIndex);
			};

			if (result == VK_SUBOPTIMAL_KHR)
				m_swapchainDirty = true;

			return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
		};

		VkCommandBuffer Command() const
		{
			return m_frames[m_frameIndex].command;
		};

		Pending* PendingOf(uint32_t target)
		{
			if (target == WINDOW)
				return &m_windowPending;

			const auto found = m_textures.find(target);

			return found == m_textures.end() ? nullptr : &found->second.pending;
		};

		void EnsureRendering(uint32_t target)
		{
			if (m_rendering && m_renderingTarget == target && m_renderingDepth)
				return;

			EndRendering();
			BeginRendering(target, true);
		};

		// Starts drawing into a target, loading what is in it or clearing it to
		// what Clear asked for.
		void BeginRendering(uint32_t target, bool withDepth)
		{
			const VkCommandBuffer command = Command();
			Pending& pending = *PendingOf(target);

			Attachment* color = nullptr;
			Attachment* depth = nullptr;
			VkImageView colorView = VK_NULL_HANDLE;
			VkImageLayout* colorLayout = nullptr;
			VkImage colorImage = VK_NULL_HANDLE;
			VkExtent2D extent;

			if (target == WINDOW)
			{
				colorImage = m_swapImages[m_imageIndex];
				colorView = m_swapViews[m_imageIndex];
				colorLayout = &m_swapLayouts[m_imageIndex];
				depth = &m_windowDepth;
				extent = m_swapExtent;
			}
			else
			{
				Texture& texture = m_textures.at(target);

				if (texture.color.image)
				{
					color = &texture.color;
					colorImage = color->image;
					colorView = color->view;
					colorLayout = &color->layout;
				};

				depth = texture.depth.image ? &texture.depth : nullptr;
				extent = { (uint32_t)texture.width, (uint32_t)texture.height };
			};

			if (!withDepth)
				depth = nullptr;

			VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };

			VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
			info.renderArea = { { 0, 0 }, extent };
			info.layerCount = 1;

			if (colorImage)
			{
				Transition(command, colorImage, VK_IMAGE_ASPECT_COLOR_BIT, *colorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				*colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				colorAttachment.imageView = colorView;
				colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachment.loadOp = pending.color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				std::copy(std::begin(pending.rgba), std::end(pending.rgba), colorAttachment.clearValue.color.float32);
				pending.color = false;

				info.colorAttachmentCount = 1;
				info.pColorAttachments = &colorAttachment;
			};

			if (depth)
			{
				Transition(command, depth->image, VK_IMAGE_ASPECT_DEPTH_BIT, depth->layout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
				depth->layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

				depthAttachment.imageView = depth->view;
				depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthAttachment.loadOp = pending.depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				depthAttachment.clearValue.depthStencil = { FAR_DEPTH, 0 };
				pending.depth = false;

				info.pDepthAttachment = &depthAttachment;
			};

			m_beginRendering(command, &info);

			m_rendering = true;
			m_renderingTarget = target;
			m_renderingDepth = depth != nullptr;
			m_renderingFormat = colorImage ? (target == WINDOW ? m_swapFormat : COLOR_TARGET_FORMAT) : VK_FORMAT_UNDEFINED;
			m_boundPipeline = VK_NULL_HANDLE;

			// The window is drawn upside down, which puts GL's bottom-up rows
			// the right way up on screen. A texture is drawn the right way up,
			// so it reads back bottom row first the way a GL one does.
			const bool flipped = target == WINDOW;
			const VkViewport viewport =
			{
				0.0f,
				flipped ? (float)extent.height : 0.0f,
				(float)extent.width,
				flipped ? -(float)extent.height : (float)extent.height,
				0.0f,
				1.0f,
			};
			const VkRect2D scissor = { { 0, 0 }, extent };

			vkCmdSetViewport(command, 0, 1, &viewport);
			vkCmdSetScissor(command, 0, 1, &scissor);
			vkCmdSetDepthBias(command, m_biasConstant, 0.0f, m_biasSlope);
		};

		void EndRendering()
		{
			if (!m_rendering)
				return;

			const VkCommandBuffer command = Command();
			m_endRendering(command);
			m_rendering = false;

			if (m_renderingTarget == WINDOW)
				return;

			// Back to being sampled, which is how every texture waits between passes.
			Texture& texture = m_textures.at(m_renderingTarget);
			Attachment& sampled = texture.color.image ? texture.color : texture.depth;
			const VkImageAspectFlags aspect = texture.color.image ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;

			Transition(command, sampled.image, aspect, sampled.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			sampled.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		};

		void ClearInPass(uint32_t target)
		{
			Pending& pending = *PendingOf(target);

			std::vector<VkClearAttachment> clears;

			if (pending.color && m_renderingFormat != VK_FORMAT_UNDEFINED)
			{
				VkClearAttachment clear{ VK_IMAGE_ASPECT_COLOR_BIT, 0 };
				std::copy(std::begin(pending.rgba), std::end(pending.rgba), clear.clearValue.color.float32);
				clears.push_back(clear);
				pending.color = false;
			};

			if (pending.depth && m_renderingDepth)
			{
				VkClearAttachment clear{ VK_IMAGE_ASPECT_DEPTH_BIT, 0 };
				clear.clearValue.depthStencil = { FAR_DEPTH, 0 };
				clears.push_back(clear);
				pending.depth = false;
			};

			if (clears.empty())
				return;

			const glm::ivec2 size = TargetSize();
			const VkClearRect rect = { { { 0, 0 }, { (uint32_t)size.x, (uint32_t)size.y } }, 0, 1 };

			vkCmdClearAttachments(Command(), (uint32_t)clears.size(), clears.data(), 1, &rect);
		};

		// Everything before against everything after. Coarse, and every pass
		// boundary in a frame here is one of these.
		static void Transition(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to)
		{
			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.oldLayout = from;
			barrier.newLayout = to;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = { aspect, 0, 1, 0, 1 };

			vkCmdPipelineBarrier(
				command,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);
		};

		VkPipeline PipelineFor(Program& program, VkPrimitiveTopology topology, uint32_t target)
		{
			const bool flipped = target == WINDOW;
			const uint64_t key =
				(uint64_t)topology
				| ((uint64_t)m_renderingFormat << FORMAT_SHIFT)
				| ((uint64_t)m_renderingDepth << DEPTH_SHIFT)
				| ((uint64_t)flipped << FLIP_SHIFT);

			const auto found = program.pipelines.find(key);

			if (found != program.pipelines.end())
				return found->second;

			const float flip = flipped ? -1.0f : 1.0f;
			const VkSpecializationMapEntry entry = { FLIP_CONSTANT_ID, 0, sizeof(flip) };

			VkSpecializationInfo specialization{ };
			specialization.mapEntryCount = 1;
			specialization.pMapEntries = &entry;
			specialization.dataSize = sizeof(flip);
			specialization.pData = &flip;

			VkPipelineShaderStageCreateInfo stages[STAGES] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = program.modules[0];
			stages[0].pName = "main";
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = program.modules[1];
			stages[1].pName = "main";
			stages[1].pSpecializationInfo = &specialization;

			const VkVertexInputBindingDescription binding = { 0, POSITION_STRIDE, VK_VERTEX_INPUT_RATE_VERTEX };
			const VkVertexInputAttributeDescription attribute = { POSITION_LOCATION, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

			VkPipelineVertexInputStateCreateInfo input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

			if (program.hasPosition)
			{
				input.vertexBindingDescriptionCount = 1;
				input.pVertexBindingDescriptions = &binding;
				input.vertexAttributeDescriptionCount = 1;
				input.pVertexAttributeDescriptions = &attribute;
			};

			VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			assembly.topology = topology;

			VkPipelineViewportDepthClipControlCreateInfoEXT depthClip{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT };
			depthClip.negativeOneToOne = VK_TRUE;

			VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			viewport.pNext = &depthClip;
			viewport.viewportCount = 1;
			viewport.scissorCount = 1;

			// Flipping the viewport turns GL's counter-clockwise front faces
			// clockwise.
			VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = VK_CULL_MODE_NONE;
			raster.frontFace = flipped ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
			raster.depthBiasEnable = VK_TRUE;
			raster.lineWidth = 1.0f;

			VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
			depth.depthTestEnable = m_renderingDepth;
			depth.depthWriteEnable = m_renderingDepth;
			depth.depthCompareOp = VK_COMPARE_OP_LESS;

			VkPipelineColorBlendAttachmentState blendAttachment{ };
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			const bool hasColor = m_renderingFormat != VK_FORMAT_UNDEFINED;

			VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
			blend.attachmentCount = hasColor ? 1 : 0;
			blend.pAttachments = &blendAttachment;

			const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };

			VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamic.dynamicStateCount = (uint32_t)std::size(dynamics);
			dynamic.pDynamicStates = dynamics;

			VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			rendering.colorAttachmentCount = hasColor ? 1 : 0;
			rendering.pColorAttachmentFormats = &m_renderingFormat;
			rendering.depthAttachmentFormat = m_renderingDepth ? DEPTH_FORMAT : VK_FORMAT_UNDEFINED;

			VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			info.pNext = &rendering;
			info.stageCount = (uint32_t)std::size(stages);
			info.pStages = stages;
			info.pVertexInputState = &input;
			info.pInputAssemblyState = &assembly;
			info.pViewportState = &viewport;
			info.pRasterizationState = &raster;
			info.pMultisampleState = &multisample;
			info.pDepthStencilState = &depth;
			info.pColorBlendState = &blend;
			info.pDynamicState = &dynamic;
			info.layout = program.layout;

			VkPipeline pipeline = VK_NULL_HANDLE;

			// Remembered either way, so a pipeline the driver refuses is reported
			// once rather than every frame.
			if (!Succeeded(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), ("creating a pipeline for " + program.name).c_str()))
				pipeline = VK_NULL_HANDLE;

			program.pipelines.emplace(key, pipeline);

			return pipeline;
		};

		// Takes one stage's uniforms, samplers and inputs into the program.
		void Reflect(Program& program, const std::vector<uint32_t>& code)
		{
			SpvReflectShaderModule module;

			if (spvReflectCreateShaderModule(code.size() * sizeof(uint32_t), code.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
				throw std::runtime_error("Could not reflect " + program.name);

			uint32_t count = 0;
			spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);

			std::vector<SpvReflectDescriptorBinding*> bindings(count);
			spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

			for (const SpvReflectDescriptorBinding* binding : bindings)
			{
				if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding->binding == DEFAULT_BLOCK_BINDING)
				{
					program.blockSize = std::max(program.blockSize, binding->block.padded_size);

					for (uint32_t i = 0; i < binding->block.member_count; i++)
					{
						const SpvReflectBlockVariable& member = binding->block.members[i];
						program.members[member.name] = { member.offset, member.size };
					};
				}
				else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
					program.samplers[binding->name] = { binding->binding, binding->image.depth == 1 };
				else
				{
					spvReflectDestroyShaderModule(&module);
					throw std::runtime_error("Shader error in " + program.name + ": " + (binding->name ? binding->name : "a resource") + " is a kind of uniform Loom does not set");
				};
			};

			if (module.shader_stage == SPV_REFLECT_SHADER_STAGE_VERTEX_BIT)
			{
				spvReflectEnumerateInputVariables(&module, &count, nullptr);

				std::vector<SpvReflectInterfaceVariable*> inputs(count);
				spvReflectEnumerateInputVariables(&module, &count, inputs.data());

				for (const SpvReflectInterfaceVariable* variable : inputs)
					if (!(variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) && variable->location == POSITION_LOCATION)
						program.hasPosition = true;
			};

			spvReflectDestroyShaderModule(&module);
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

		const Texture& SampledTexture(const Program::Sampler& sampler) const
		{
			const auto found = m_textures.find(sampler.texture);

			// Whatever is behind it has to match the sampler's kind: a shadow
			// sampler reads a depth texture.
			if (found != m_textures.end() && (found->second.color.image == VK_NULL_HANDLE) == sampler.shadow)
				return found->second;

			return m_textures.at(sampler.shadow ? m_fallbackDepth : m_fallbackColor);
		};

		uint32_t Add(Texture&& texture)
		{
			const uint32_t handle = m_nextHandle++;
			m_textures.emplace(handle, std::move(texture));

			return handle;
		};

		Allocation Allocate(VkDeviceSize size, VkDeviceSize alignment)
		{
			Frame& frame = m_frames[m_frameIndex];

			while (frame.chunkIndex < frame.chunks.size())
			{
				Chunk& chunk = frame.chunks[frame.chunkIndex];
				const VkDeviceSize offset = (chunk.used + alignment - 1) / alignment * alignment;

				if (offset + size <= chunk.size)
				{
					chunk.used = offset + size;
					return { chunk.buffer, offset, chunk.mapped + offset };
				};

				frame.chunkIndex++;
			};

			Chunk chunk;
			chunk.size = std::max(CHUNK_SIZE, size);

			void* mapped = nullptr;
			CreateBuffer(chunk.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, chunk.buffer, chunk.memory, &mapped);

			chunk.mapped = (uint8_t*)mapped;
			chunk.used = size;
			frame.chunks.push_back(chunk);
			frame.chunkIndex = frame.chunks.size() - 1;

			return { chunk.buffer, 0, chunk.mapped };
		};

		uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
		{
			VkPhysicalDeviceMemoryProperties properties;
			vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &properties);

			for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
				if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
					return i;

			return UINT32_MAX;
		};

		bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory, void** mapped)
		{
			VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			info.size = size;
			info.usage = usage;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (!Succeeded(vkCreateBuffer(m_device, &info, nullptr, &buffer), "creating a buffer"))
				return false;

			VkMemoryRequirements requirements;
			vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

			VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			if (!Succeeded(vkAllocateMemory(m_device, &allocate, nullptr, &memory), "allocating buffer memory"))
			{
				vkDestroyBuffer(m_device, buffer, nullptr);
				buffer = VK_NULL_HANDLE;
				return false;
			};

			vkBindBufferMemory(m_device, buffer, memory, 0);
			vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, mapped);

			return true;
		};

		void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
		{
			if (buffer)	vkDestroyBuffer(m_device, buffer, nullptr);
			if (memory)	vkFreeMemory(m_device, memory, nullptr);
		};

		VkImageView CreateView(VkImage image, VkFormat format, VkImageAspectFlags aspect)
		{
			VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			info.image = image;
			info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			info.format = format;
			info.subresourceRange = { aspect, 0, 1, 0, 1 };

			VkImageView view = VK_NULL_HANDLE;
			vkCreateImageView(m_device, &info, nullptr, &view);

			return view;
		};

		bool CreateAttachment(Attachment& attachment, VkFormat format, int width, int height, VkImageUsageFlags usage, VkImageAspectFlags aspect)
		{
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = format;
			info.extent = { (uint32_t)width, (uint32_t)height, 1 };
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = usage;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			if (vkCreateImage(m_device, &info, nullptr, &attachment.image) != VK_SUCCESS)
				return false;

			VkMemoryRequirements requirements;
			vkGetImageMemoryRequirements(m_device, attachment.image, &requirements);

			VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			if (vkAllocateMemory(m_device, &allocate, nullptr, &attachment.memory) != VK_SUCCESS)
			{
				DestroyAttachment(attachment);
				return false;
			};

			vkBindImageMemory(m_device, attachment.image, attachment.memory, 0);

			attachment.view = CreateView(attachment.image, format, aspect);
			attachment.format = format;
			attachment.layout = VK_IMAGE_LAYOUT_UNDEFINED;

			return true;
		};

		void DestroyAttachment(Attachment& attachment)
		{
			if (attachment.view)	vkDestroyImageView(m_device, attachment.view, nullptr);
			if (attachment.image)	vkDestroyImage(m_device, attachment.image, nullptr);
			if (attachment.memory)	vkFreeMemory(m_device, attachment.memory, nullptr);

			attachment = Attachment{ };
		};

		// A shadow sampler compares rather than reads, and linear filtering on
		// a compared texture is a free 2x2 PCF.
		VkSampler CreateSampler(bool compare)
		{
			VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			info.magFilter = VK_FILTER_LINEAR;
			info.minFilter = VK_FILTER_LINEAR;
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.compareEnable = compare;
			info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

			VkSampler sampler = VK_NULL_HANDLE;
			vkCreateSampler(m_device, &info, nullptr, &sampler);

			return sampler;
		};

		void DestroyTextureNow(Texture& texture)
		{
			if (texture.imgui && m_imgui)
				ImGui_ImplVulkan_RemoveTexture(texture.imgui);

			if (texture.sampler)
				vkDestroySampler(m_device, texture.sampler, nullptr);

			DestroyAttachment(texture.color);
			DestroyAttachment(texture.depth);
		};

		void DestroyProgram(Program& program)
		{
			for (auto& [key, pipeline] : program.pipelines)
				if (pipeline)
					vkDestroyPipeline(m_device, pipeline, nullptr);

			for (VkShaderModule module : program.modules)
				if (module)
					vkDestroyShaderModule(m_device, module, nullptr);

			vkDestroyPipelineLayout(m_device, program.layout, nullptr);
			vkDestroyDescriptorSetLayout(m_device, program.setLayout, nullptr);
		};

		// Destroys something once the frames that may still be using it are done.
		void Defer(std::function<void()> deletion)
		{
			m_frames[m_frameIndex].deletions.push_back(std::move(deletion));
		};

		void RunDeletions(Frame& frame)
		{
			for (std::function<void()>& deletion : frame.deletions)
				deletion();

			frame.deletions.clear();
		};

		// Records and runs commands outside the frame, waiting for them: for
		// setting a resource up, which can happen in the middle of a pass.
		template<typename Record>
		void Immediately(const Record& record)
		{
			VkCommandBufferAllocateInfo allocate{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocate.commandPool = m_immediatePool;
			allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocate.commandBufferCount = 1;

			VkCommandBuffer command = VK_NULL_HANDLE;
			vkAllocateCommandBuffers(m_device, &allocate, &command);

			VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(command, &begin);

			record(command);

			vkEndCommandBuffer(command);

			VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &command;

			vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(m_queue);

			vkFreeCommandBuffers(m_device, m_immediatePool, 1, &command);
		};

		Readback RecordReadback(VkCommandBuffer command)
		{
			Readback readback;
			readback.width = (int)m_swapExtent.width;
			readback.height = (int)m_swapExtent.height;

			const VkDeviceSize size = (VkDeviceSize)readback.width * readback.height * sizeof(uint32_t);
			void* mapped = nullptr;

			if (!CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback.buffer, readback.memory, &mapped))
				return Readback{ };

			readback.mapped = (uint8_t*)mapped;

			VkImage image = m_swapImages[m_imageIndex];
			Transition(command, image, VK_IMAGE_ASPECT_COLOR_BIT, m_swapLayouts[m_imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			m_swapLayouts[m_imageIndex] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

			VkBufferImageCopy copy{ };
			copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copy.imageExtent = { m_swapExtent.width, m_swapExtent.height, 1 };
			vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);

			return readback;
		};

		// Hands the window's pixels over the way GL reads them: red first and
		// bottom row first.
		void DeliverReadback(Readback& readback)
		{
			constexpr int BYTES_PER_PIXEL = 3;
			enum { RED, GREEN, BLUE };

			const CaptureHandler handler = std::move(m_capture);
			m_capture = nullptr;

			std::vector<uint8_t> rgb((size_t)readback.width * readback.height * BYTES_PER_PIXEL);
			const bool bgra = m_swapFormat == VK_FORMAT_B8G8R8A8_UNORM;

			for (int y = 0; y < readback.height && readback.mapped; y++)
			{
				const uint8_t* from = readback.mapped + (size_t)(readback.height - 1 - y) * readback.width * sizeof(uint32_t);
				uint8_t* to = rgb.data() + (size_t)y * readback.width * BYTES_PER_PIXEL;

				for (int x = 0; x < readback.width; x++, from += sizeof(uint32_t), to += BYTES_PER_PIXEL)
				{
					to[RED] = from[bgra ? BLUE : RED];
					to[GREEN] = from[GREEN];
					to[BLUE] = from[bgra ? RED : BLUE];
				};
			};

			DestroyBuffer(readback.buffer, readback.memory);

			if (!readback.mapped)
				rgb.clear();

			handler(readback.mapped ? readback.width : 0, readback.mapped ? readback.height : 0, rgb);
		};

		GLFWwindow* m_window = nullptr;

		VkInstance m_instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		VkDevice m_device = VK_NULL_HANDLE;
		uint32_t m_queueFamily = 0;
		VkQueue m_queue = VK_NULL_HANDLE;

		PFN_vkCmdPushDescriptorSetKHR m_pushDescriptorSet = nullptr;
		PFN_vkCmdBeginRendering m_beginRendering = nullptr;
		PFN_vkCmdEndRendering m_endRendering = nullptr;

		VkDeviceSize m_uniformAlignment = 1;
		int m_maxTextureSize = 0;
		bool m_glslang = false;

		VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
		VkFormat m_swapFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_swapExtent = { 0, 0 };
		uint32_t m_minImageCount = 0;
		std::vector<VkImage> m_swapImages;
		std::vector<VkImageView> m_swapViews;
		std::vector<VkImageLayout> m_swapLayouts;
		std::vector<VkSemaphore> m_rendered;
		Attachment m_windowDepth;
		bool m_swapchainDirty = false;

		std::array<Frame, FRAMES_IN_FLIGHT> m_frames;
		uint32_t m_frameIndex = 0;
		uint32_t m_imageIndex = 0;
		VkCommandPool m_immediatePool = VK_NULL_HANDLE;

		bool m_recording = false;
		bool m_haveImage = false;
		Pending m_windowPending;

		// What is being drawn into, the window at the bottom.
		std::vector<uint32_t> m_stack = { WINDOW };

		bool m_rendering = false;
		uint32_t m_renderingTarget = WINDOW;
		bool m_renderingDepth = false;
		VkFormat m_renderingFormat = VK_FORMAT_UNDEFINED;
		VkPipeline m_boundPipeline = VK_NULL_HANDLE;

		std::vector<VkWriteDescriptorSet> m_writes;
		std::vector<VkDescriptorImageInfo> m_images;

		float m_biasSlope = 0.0f;
		float m_biasConstant = 0.0f;

		CaptureHandler m_capture;

		VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
		bool m_imgui = false;

		uint32_t m_nextHandle = 1;
		std::unordered_map<uint32_t, Program> m_programs;
		std::unordered_map<uint32_t, Texture> m_textures;
		uint32_t m_fallbackColor = 0;
		uint32_t m_fallbackDepth = 0;
	};

	std::unique_ptr<Renderer> CreateVulkanRenderer()
	{
		return std::make_unique<VulkanRenderer>();
	};
};

#endif

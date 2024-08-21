import Engine;
import DataTypes;

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <vulkan/vulkan.hpp>

import <iostream>;


namespace Loom
{
	static inline VkInstance vk_instance{ };

	struct Application
	{
		VkInstance vk_instance;
		std::vector<VkPhysicalDevice> physicalDevices;
	};

	void Engine::Init()
	{
		////////////////////////////////////////////////////////////////////////////////////////////////////
	   ///
	  /// Vulkan Initialization
	 ///
	////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////
	   ///
	  /// Vulkan: Getting Extensions
	 ///
	////////////////////////////////////////////////////////////////////////////////////////////////////

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;

		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> requiredExtensions;

		for (uint32_t i = 0; i < glfwExtensionCount; i++)
			requiredExtensions.emplace_back(glfwExtensions[i]);

		requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> extensions(extensionCount);

		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

		std::cout << "available extensions:\n";

		for (const auto& extension : extensions)
			std::cout << '\t' << extension.extensionName << '\n';

		{
			VkApplicationInfo vk_appInfo{ };
			{
				vk_appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				vk_appInfo.pApplicationName = "Test Vulkan Instance";
				vk_appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
				vk_appInfo.pEngineName = "No Engine";
				vk_appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
				vk_appInfo.apiVersion = VK_API_VERSION_1_0;
			};

			VkInstanceCreateInfo vk_createInfo{};
			{
				vk_createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				vk_createInfo.pApplicationInfo = &vk_appInfo;
				vk_createInfo.enabledExtensionCount = glfwExtensionCount;
				vk_createInfo.ppEnabledExtensionNames = glfwExtensions;
				vk_createInfo.enabledLayerCount = 0;
				vk_createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
				vk_createInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
				vk_createInfo.ppEnabledExtensionNames = requiredExtensions.data();
			};

			if (vkCreateInstance(&vk_createInfo, nullptr, &vk_instance) != VK_SUCCESS)
				throw std::runtime_error("failed to create instance!");
		};

		////////////////////////////////////////////////////////////////////////////////////////////////////
	   ///
	  /// Vulkan: Getting Physical Devices
	 ///
	////////////////////////////////////////////////////////////////////////////////////////////////////

		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(vk_instance, &deviceCount, nullptr);

		if (deviceCount == 0) throw std::runtime_error("failed to find GPUs with Vulkan support!");

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(vk_instance, &deviceCount, devices.data());

		for (auto& device : devices)
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(device, &properties);
			std::cout << properties.deviceName << std::endl;

			switch (properties.deviceType)
			{
			case 1:
			case 2:
			case 3:
				goto found_gpu;
				break;
			};

			continue;

		found_gpu:
			physicalDevice = device;
			break;
		};

		if (physicalDevice == VK_NULL_HANDLE)
			throw std::runtime_error("failed to find a suitable GPU!");

		////////////////////////////////////////////////////////////////////////////////////////////////////
	   ///
	  /// Vulkan: Generating Surface
	 ///
	////////////////////////////////////////////////////////////////////////////////////////////////////

		VkSurfaceKHR surface;
		{
			VkWin32SurfaceCreateInfoKHR vk_createInfo{ };
			{
				vk_createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
				//vk_createInfo.hwnd = glfwGetWin32Window(window);
				vk_createInfo.hinstance = GetModuleHandle(nullptr);
			};
		};
	};

	void Engine::Clean()
	{
		vkDestroyInstance(vk_instance, nullptr);
	};


	//void Engine::UpdateAll()
	//{
	//	
	//};

	//void Engine::RenderAll()
	//{
	//	
	//};
};

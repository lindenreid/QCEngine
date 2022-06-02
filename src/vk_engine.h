﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>

class VulkanEngine {
public:
	// Vulkan environment
	VkInstance _instance; // vulkan library
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as default device
	VkDevice _device; // handle to drivers for commands
	VkSurfaceKHR _surface; // Vulkan window surface

	// Swapchain
	VkSwapchainKHR _swapchain; // Vulkan swapchain - images able to display to screen
	VkFormat _swapchainImageFormat; // img format expected by window system
	std::vector<VkImage> _swapchainImages; // images in swapchain
	std::vector<VkImageView> _swapchainImageViews; // image-views from swapchain

	bool _isInitialized{ false };
	int _frameNumber {0};

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

private:
	void init_vulkan();
	void init_swapchain();
};

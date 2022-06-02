// immediately log error and abort if there's a vulkan error
#define VK_CHECK(x)												\
	do															\
	{															\
		VkResult err = x;										\
		if (err)												\
		{														\
			std::cout << "Vulkan error: " << err << std::endl;	\
		}														\
	}	while (0)												\

#include <iostream>
#include <vector>

#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include "VkBootstrap.h"

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"QCEngine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	// load core Vulkan structures & command queue
	init_vulkan();

	// create swapchain
	init_swapchain();

	// init renderpass
	init_default_renderpass();

	// init framebuffers
	init_framebuffers();

	// init command buffers
	init_commands();
	
	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
	// tool from the VkBootstrap library, simplifies the creation of a VkInstance
	vkb::InstanceBuilder builder;

	// make Vulkan instance with basic debug features
	auto inst_ret = builder.set_app_name("QC Engine")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	// store these so we can release them at program exit
	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	// get the surface of the window we opened with SDL in init()
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	// use vkbootstrap to select a GPU compatible with our SDL surface and Vulkan version
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(_surface)
		.select()
		.value();

	// create Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();

	// get the VkDevice handle used in the rest of the Vulkan application
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	// use vkbootstrap to get a graphics queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder(_chosenGPU, _device, _surface);

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // hard VSYNC
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	// store swapchain & images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;
}

void VulkanEngine::init_commands()
{
	// create a command pool for cmds submitted to the graphics queue
	// VkCommandPoolCreateInfo is a structure to specify params for a newly created cmd pool
	VkCommandPoolCreateInfo cmdPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(vkCreateCommandPool(_device, &cmdPoolInfo, nullptr, &_commandPool)); // note _commandPool gets overwritten here

	// allocate the default cmd buff for rendering
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_commandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));

}

void VulkanEngine::init_default_renderpass()
{
	// create description for color pass
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _swapchainImageFormat; // needs to be compatible with the swapchain format
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT; // 1 sample; no MSAA
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep attachment when renderpass ends
	
	// no stencil
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// don't care about starting layout
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// after renderpass ends, image needs to be ready for display
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	// attachment number is index in pAttachments array in parent renderpass
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// create 1 subpass (required 1)
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	// connect color attachment and subpass to info
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));
}

void VulkanEngine::init_framebuffers()
{
	// create framebuffers for swapchain images
	// connect renderpass to the images for rendering
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	// read # of images in swapchain
	const uint32_t swapchain_image_count = _swapchainImages.size();

	// create framebuffers for each swapchain image view
	_framebuffers = std::vector<VkFramebuffer>(swapchain_image_count);
	for (int i = 0; i < swapchain_image_count; i++)
	{
		fb_info.pAttachments = &_swapchainImageViews[i];
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));
	}
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized)
	{
		vkDestroyCommandPool(_device, _commandPool, nullptr);

		vkDestroySwapchainKHR(_device, _swapchain, nullptr);

		vkDestroyRenderPass(_device, _renderPass, nullptr);

		// destroy swapchain resources
		for (int i = 0; i < _swapchainImageViews.size(); i++)
		{
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		}

		// MUST destroy these in the opposite order they're created
		// VkPhysicalDevice doesn't need to be destroyed- it's just a pointer to a driver
		vkDestroyDevice(_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	// nothing yet
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit)
	{
		// Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			// close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT) bQuit = true;
		}

		draw();
	}
}


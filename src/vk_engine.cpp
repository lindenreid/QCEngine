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
#include <fstream>

#include "vk_engine.h"
#include "PipelineBuilder.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include "VkBootstrap.h"

// continue lesson from: https://vkguide.dev/docs/chapter-2/vulkan_render_pipeline/
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

	// init structures to sync frame rendering with CPU
	init_sync_structures();

	// load shaders
	init_pipelines();
	
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

void VulkanEngine::init_sync_structures()
{
	// create synchronization structures
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	// create the fence with the Create Signaled flag, so we can wait on it to be ready before using it on a GPU command
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

	// don't need flags for the semaphores themselves
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	// load the file onto buffer
	size_t fileSize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	// load the shader into vulkan
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

void VulkanEngine::init_pipelines()
{
	VkShaderModule helloTriangleFragShader;
	if (!load_shader_module("../../shaders/helloTriangle.frag.spv", &helloTriangleFragShader))
	{
		std::cout << "Error building triangle frag shader." << std::endl;
	}
	else
	{
		std::cout << "Hello triangle frag shader successfully loaded." << std::endl;
	}

	VkShaderModule helloTriangleVertexShader;
	if (!load_shader_module("../../shaders/helloTriangle.vert.spv", &helloTriangleVertexShader))
	{
		std::cout << "Error building triangle vert shader." << std::endl;
	}
	else
	{
		std::cout << "Hello triangle vert shader successfully loaded." << std::endl;
	}

	VkShaderModule altHelloFragShader;
	if (!load_shader_module("../../shaders/helloTriangleV2.frag.spv", &altHelloFragShader))
	{
		std::cout << "Error building triangle 2 frag shader." << std::endl;
	}
	else
	{
		std::cout << "Hello triangle frag 2 shader successfully loaded." << std::endl;
	}

	VkShaderModule altHelloVertexShader;
	if (!load_shader_module("../../shaders/helloTriangleV2.vert.spv", &altHelloVertexShader))
	{
		std::cout << "Error building triangle 2 vert shader." << std::endl;
	}
	else
	{
		std::cout << "Hello triangle vert 2 shader successfully loaded." << std::endl;
	}

	// build the pipeline layout that controls inputs/outputs of the shader
	// just build empty default for now
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

	// build the stage-create-info for both vertex and frag stages
	// defines shader modules per stage
	PipelineBuilder pipelineBuilder;

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, helloTriangleVertexShader)
	);

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, helloTriangleFragShader)
	);

	// controls vertex buffer attributes - not using yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	// config for what kind of geo to draw (tris/lines/points)
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	// build viewport and scissor from swapchain dimensions
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent; //note: scissor might want to be larger?

	// draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	// don't use MSAA, just use default
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	// single blend attachment with no blending, write to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

	// build pipeline
	_trianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	// use same builder to build second pipeline, but for the other triangle shader
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, altHelloVertexShader)
	);

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, altHelloFragShader)
	);

	_altTrianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized)
	{
		// make sure GPU is done
		vkDeviceWaitIdle(_device);

		// then destroy all the stuff we created

		vkDestroyCommandPool(_device, _commandPool, nullptr);

		vkDestroyFence(_device, _renderFence, nullptr);
		vkDestroySemaphore(_device, _renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _presentSemaphore, nullptr);

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
		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	// wait until GPU has finished rendering last frame 
	VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &_renderFence)); // reset fence after waiting- must be reset between uses

	// acquire image index from swapchain
	// wait for up to [timeout] amt of time for an image- this is FPS lock
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, _presentSemaphore, nullptr, &swapchainImageIndex));

	// now that we're confident the previous cmds finished executing, reset cmd buff to start recording again
	VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

	// begin command buffer recording 
	VkCommandBuffer cmd = _mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	// we're using this cmd buff exactly once, so let Vulkan know- helps with optimization, esp since we're using every frame
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// make a frame clear color from frame #, to animate it 
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { {0.0f, 0.0f, flash, 1.0f} };

	// start main renderpass
	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;

	rpInfo.renderPass = _renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = _windowExtent;
	rpInfo.framebuffer = _framebuffers[swapchainImageIndex];

	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	// RENDER COMMANDS ------------------------------------- v

	if (_selectedShader == 0)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
	}
	else
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _altTrianglePipeline);
	}
	vkCmdDraw(cmd, 3, 1, 0, 0);

	// RENDER COMMANDS ------------------------------------- ^

	// finalize renderpass
	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	// prepare submission to the queue
	// wait on the _presentSemaphore, which is signaled when the swapchain is ready
	// then signal _renderSemaphore, which indicates that rendering has finished

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &_presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	// submit command buffer to queue and execute it 
	// _renderFence will now block until the graphic commands finish execution (see beginning of frame)
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

	// display image we just rendered in the visible window!!
	// wait for _renderSemaphore, ensuring that drawing commands finish before displaying image
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
	
	// for our clear color animation
	_frameNumber++;
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
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if (e.type == SDL_KEYDOWN)
			{
				switch (e.key.keysym.sym)
				{
				case SDLK_SPACE:
					_selectedShader += 1;
					if (_selectedShader > 1) _selectedShader = 0;
				}
			}
		}

		draw();
	}
}


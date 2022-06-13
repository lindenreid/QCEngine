// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <Mesh.h>
#include <glm/glm.hpp>

// allows us to delete Vulkan objects in the order we created them
struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& fn)
	{
		deletors.push_back(fn);
	}

	void flush()
	{
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}
		deletors.clear();
	}
};

// for pushing constant data to shaders
struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
};

class VulkanEngine {
public:
	// Vulkan environment
	VkInstance _instance; // vulkan library
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as default device
	VkDevice _device; // handle to drivers for commands
	VkSurfaceKHR _surface; // Vulkan window surface

	// swapchain
	VkSwapchainKHR _swapchain; // Vulkan swapchain - images able to display to screen
	VkFormat _swapchainImageFormat; // img format expected by window system
	std::vector<VkImage> _swapchainImages; // images in swapchain
	std::vector<VkImageView> _swapchainImageViews; // image-views from swapchain

	// command buffers
	VkQueue _graphicsQueue; // queue we will submit commands to
	uint32_t _graphicsQueueFamily; // queue family type
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	// render pass and frame buffers
	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	// synchronization
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	// pipelines
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;
	
	VkPipeline _altTrianglePipeline;
	int _selectedShader{ 0 };

	VmaAllocator _allocator;
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;
	Mesh _triangleMesh;

	// meshes
	Mesh _monkeyMesh;

	// deletion
	DeletionQueue _mainDeletionQueue;

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
	void init_commands();
	void init_default_renderpass();
	void init_framebuffers();
	void init_sync_structures();
	void init_pipelines();
	
	void load_meshes();
	void upload_mesh(Mesh& mesh);

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
};

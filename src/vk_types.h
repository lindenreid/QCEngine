// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vector>
#include <deque>
#include <functional>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct AllocatedBuffer {
	VkBuffer _buffer;
	VmaAllocation _allocation;
};

//we will add our main reusable types here
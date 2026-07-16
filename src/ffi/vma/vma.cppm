module;

#include <vulkan/vulkan.h>

#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED 1
#include <vk_mem_alloc.h>

export module vma;

export import vulkan;

export using ::VMA_MEMORY_USAGE_CPU_ONLY;
export using ::VMA_MEMORY_USAGE_GPU_ONLY;

export using ::VmaAllocation;
export using ::VmaAllocationCreateInfo;
export using ::VmaAllocationInfo;
export using ::VmaAllocator;
export using ::VmaAllocatorCreateInfo;
export using ::VmaBudget;
export using ::VmaMemoryUsage;
export using ::VmaVirtualAllocation;
export using ::VmaVirtualAllocationCreateInfo;
export using ::VmaVirtualBlock;
export using ::VmaVirtualBlockCreateInfo;

export using ::vmaClearVirtualBlock;
export using ::vmaCreateAllocator;
export using ::vmaCreateBuffer;
export using ::vmaCreateImage;
export using ::vmaCreateVirtualBlock;
export using ::vmaDestroyAllocator;
export using ::vmaDestroyBuffer;
export using ::vmaDestroyImage;
export using ::vmaDestroyVirtualBlock;
export using ::vmaFlushAllocation;
export using ::vmaGetHeapBudgets;
export using ::vmaIsVirtualBlockEmpty;
export using ::vmaMapMemory;
export using ::vmaUnmapMemory;
export using ::vmaVirtualAllocate;
export using ::vmaVirtualFree;

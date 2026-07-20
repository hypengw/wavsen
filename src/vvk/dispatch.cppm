module;

#include "vvk/macros.hpp"

export module wavsen.vvk:dispatch;

import rstd;
import rstd.log;
import vulkan;
import :handle;

using namespace rstd::prelude;

export namespace vvk
{

const char* ToString(VkResult result) noexcept;
const char* ToString(VkFormat format) noexcept;
const char* ToString(VkColorSpaceKHR color) noexcept;

struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr {};

    PFN_vkCreateInstance                       vkCreateInstance {};
    PFN_vkDestroyInstance                      vkDestroyInstance {};
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties {};
    PFN_vkEnumerateInstanceLayerProperties     vkEnumerateInstanceLayerProperties {};

    PFN_vkCreateDebugUtilsMessengerEXT            vkCreateDebugUtilsMessengerEXT {};
    PFN_vkCreateDevice                            vkCreateDevice {};
    PFN_vkDestroyDebugUtilsMessengerEXT           vkDestroyDebugUtilsMessengerEXT {};
    PFN_vkDestroyDevice                           vkDestroyDevice {};
    PFN_vkDestroySurfaceKHR                       vkDestroySurfaceKHR {};
    PFN_vkEnumerateDeviceExtensionProperties      vkEnumerateDeviceExtensionProperties {};
    PFN_vkEnumeratePhysicalDevices                vkEnumeratePhysicalDevices {};
    PFN_vkGetDeviceProcAddr                       vkGetDeviceProcAddr {};
    PFN_vkGetPhysicalDeviceFeatures2              vkGetPhysicalDeviceFeatures2 {};
    PFN_vkGetPhysicalDeviceFormatProperties       vkGetPhysicalDeviceFormatProperties {};
    PFN_vkGetPhysicalDeviceMemoryProperties       vkGetPhysicalDeviceMemoryProperties {};
    PFN_vkGetPhysicalDeviceMemoryProperties2      vkGetPhysicalDeviceMemoryProperties2 {};
    PFN_vkGetPhysicalDeviceProperties             vkGetPhysicalDeviceProperties {};
    PFN_vkGetPhysicalDeviceProperties2            vkGetPhysicalDeviceProperties2 {};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties  vkGetPhysicalDeviceQueueFamilyProperties {};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2 {};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR {};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR      vkGetPhysicalDeviceSurfaceFormatsKHR {};
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR {};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR      vkGetPhysicalDeviceSurfaceSupportKHR {};
    PFN_vkGetSwapchainImagesKHR                   vkGetSwapchainImagesKHR {};
    PFN_vkQueuePresentKHR                         vkQueuePresentKHR {};
};

struct DeviceDispatch : InstanceDispatch {
    PFN_vkAcquireNextImageKHR                    vkAcquireNextImageKHR {};
    PFN_vkAllocateCommandBuffers                 vkAllocateCommandBuffers {};
    PFN_vkAllocateDescriptorSets                 vkAllocateDescriptorSets {};
    PFN_vkAllocateMemory                         vkAllocateMemory {};
    PFN_vkBeginCommandBuffer                     vkBeginCommandBuffer {};
    PFN_vkBindBufferMemory                       vkBindBufferMemory {};
    PFN_vkBindImageMemory                        vkBindImageMemory {};
    PFN_vkBindImageMemory2                       vkBindImageMemory2 {};
    PFN_vkCmdBeginDebugUtilsLabelEXT             vkCmdBeginDebugUtilsLabelEXT {};
    PFN_vkCmdBeginQuery                          vkCmdBeginQuery {};
    PFN_vkCmdBeginRenderPass                     vkCmdBeginRenderPass {};
    PFN_vkCmdBindDescriptorSets                  vkCmdBindDescriptorSets {};
    PFN_vkCmdBindIndexBuffer                     vkCmdBindIndexBuffer {};
    PFN_vkCmdBindPipeline                        vkCmdBindPipeline {};
    PFN_vkCmdBindVertexBuffers                   vkCmdBindVertexBuffers {};
    PFN_vkCmdBlitImage                           vkCmdBlitImage {};
    PFN_vkCmdClearColorImage                     vkCmdClearColorImage {};
    PFN_vkCmdClearAttachments                    vkCmdClearAttachments {};
    PFN_vkCmdCopyBuffer                          vkCmdCopyBuffer {};
    PFN_vkCmdCopyBufferToImage                   vkCmdCopyBufferToImage {};
    PFN_vkCmdCopyImage                           vkCmdCopyImage {};
    PFN_vkCmdCopyImageToBuffer                   vkCmdCopyImageToBuffer {};
    PFN_vkCmdDispatch                            vkCmdDispatch {};
    PFN_vkCmdDraw                                vkCmdDraw {};
    PFN_vkCmdDrawIndexed                         vkCmdDrawIndexed {};
    PFN_vkCmdEndDebugUtilsLabelEXT               vkCmdEndDebugUtilsLabelEXT {};
    PFN_vkCmdEndQuery                            vkCmdEndQuery {};
    PFN_vkCmdEndRenderPass                       vkCmdEndRenderPass {};
    PFN_vkCmdFillBuffer                          vkCmdFillBuffer {};
    PFN_vkCmdPipelineBarrier                     vkCmdPipelineBarrier {};
    PFN_vkCmdPipelineBarrier2                    vkCmdPipelineBarrier2 {};
    PFN_vkCmdPushConstants                       vkCmdPushConstants {};
    PFN_vkCmdPushDescriptorSetKHR                vkCmdPushDescriptorSetKHR {};
    PFN_vkCmdPushDescriptorSetWithTemplateKHR    vkCmdPushDescriptorSetWithTemplateKHR {};
    PFN_vkCmdResolveImage                        vkCmdResolveImage {};
    PFN_vkCmdSetBlendConstants                   vkCmdSetBlendConstants {};
    PFN_vkCmdSetDepthBias                        vkCmdSetDepthBias {};
    PFN_vkCmdSetDepthBounds                      vkCmdSetDepthBounds {};
    PFN_vkCmdSetEvent                            vkCmdSetEvent {};
    PFN_vkCmdSetLineWidth                        vkCmdSetLineWidth {};
    PFN_vkCmdSetScissor                          vkCmdSetScissor {};
    PFN_vkCmdSetStencilCompareMask               vkCmdSetStencilCompareMask {};
    PFN_vkCmdSetStencilReference                 vkCmdSetStencilReference {};
    PFN_vkCmdSetStencilWriteMask                 vkCmdSetStencilWriteMask {};
    PFN_vkCmdSetViewport                         vkCmdSetViewport {};
    PFN_vkCmdWaitEvents                          vkCmdWaitEvents {};
    PFN_vkCreateBuffer                           vkCreateBuffer {};
    PFN_vkCreateBufferView                       vkCreateBufferView {};
    PFN_vkCreateCommandPool                      vkCreateCommandPool {};
    PFN_vkCreateComputePipelines                 vkCreateComputePipelines {};
    PFN_vkCreateDescriptorPool                   vkCreateDescriptorPool {};
    PFN_vkCreateDescriptorSetLayout              vkCreateDescriptorSetLayout {};
    PFN_vkCreateDescriptorUpdateTemplateKHR      vkCreateDescriptorUpdateTemplateKHR {};
    PFN_vkCreateEvent                            vkCreateEvent {};
    PFN_vkCreateFence                            vkCreateFence {};
    PFN_vkCreateFramebuffer                      vkCreateFramebuffer {};
    PFN_vkCreateGraphicsPipelines                vkCreateGraphicsPipelines {};
    PFN_vkCreateImage                            vkCreateImage {};
    PFN_vkCreateImageView                        vkCreateImageView {};
    PFN_vkCreatePipelineLayout                   vkCreatePipelineLayout {};
    PFN_vkCreateQueryPool                        vkCreateQueryPool {};
    PFN_vkCreateRenderPass                       vkCreateRenderPass {};
    PFN_vkCreateSampler                          vkCreateSampler {};
    PFN_vkCreateSemaphore                        vkCreateSemaphore {};
    PFN_vkCreateShaderModule                     vkCreateShaderModule {};
    PFN_vkCreateSwapchainKHR                     vkCreateSwapchainKHR {};
    PFN_vkDestroyBuffer                          vkDestroyBuffer {};
    PFN_vkDestroyBufferView                      vkDestroyBufferView {};
    PFN_vkDestroyCommandPool                     vkDestroyCommandPool {};
    PFN_vkDestroyDescriptorPool                  vkDestroyDescriptorPool {};
    PFN_vkDestroyDescriptorSetLayout             vkDestroyDescriptorSetLayout {};
    PFN_vkDestroyDescriptorUpdateTemplateKHR     vkDestroyDescriptorUpdateTemplateKHR {};
    PFN_vkDestroyEvent                           vkDestroyEvent {};
    PFN_vkDestroyFence                           vkDestroyFence {};
    PFN_vkDestroyFramebuffer                     vkDestroyFramebuffer {};
    PFN_vkDestroyImage                           vkDestroyImage {};
    PFN_vkDestroyImageView                       vkDestroyImageView {};
    PFN_vkDestroyPipeline                        vkDestroyPipeline {};
    PFN_vkDestroyPipelineLayout                  vkDestroyPipelineLayout {};
    PFN_vkDestroyQueryPool                       vkDestroyQueryPool {};
    PFN_vkDestroyRenderPass                      vkDestroyRenderPass {};
    PFN_vkDestroySampler                         vkDestroySampler {};
    PFN_vkDestroySemaphore                       vkDestroySemaphore {};
    PFN_vkDestroyShaderModule                    vkDestroyShaderModule {};
    PFN_vkDestroySwapchainKHR                    vkDestroySwapchainKHR {};
    PFN_vkDeviceWaitIdle                         vkDeviceWaitIdle {};
    PFN_vkEndCommandBuffer                       vkEndCommandBuffer {};
    PFN_vkFreeCommandBuffers                     vkFreeCommandBuffers {};
    PFN_vkFreeDescriptorSets                     vkFreeDescriptorSets {};
    PFN_vkFreeMemory                             vkFreeMemory {};
    PFN_vkGetBufferMemoryRequirements2           vkGetBufferMemoryRequirements2 {};
    PFN_vkGetDeviceQueue                         vkGetDeviceQueue {};
    PFN_vkGetEventStatus                         vkGetEventStatus {};
    PFN_vkGetFenceStatus                         vkGetFenceStatus {};
    PFN_vkGetImageMemoryRequirements             vkGetImageMemoryRequirements {};
    PFN_vkGetImageMemoryRequirements2            vkGetImageMemoryRequirements2 {};
    PFN_vkGetImageSubresourceLayout              vkGetImageSubresourceLayout {};
    PFN_vkGetMemoryFdKHR                         vkGetMemoryFdKHR {};
    PFN_vkGetMemoryFdPropertiesKHR               vkGetMemoryFdPropertiesKHR {};
    PFN_vkGetSemaphoreFdKHR                      vkGetSemaphoreFdKHR {};
    PFN_vkGetImageDrmFormatModifierPropertiesEXT vkGetImageDrmFormatModifierPropertiesEXT {};
    PFN_vkGetPipelineExecutablePropertiesKHR     vkGetPipelineExecutablePropertiesKHR {};
    PFN_vkGetPipelineExecutableStatisticsKHR     vkGetPipelineExecutableStatisticsKHR {};
    PFN_vkGetQueryPoolResults                    vkGetQueryPoolResults {};
    PFN_vkGetSemaphoreCounterValueKHR            vkGetSemaphoreCounterValueKHR {};
    PFN_vkMapMemory                              vkMapMemory {};
    PFN_vkQueueSubmit                            vkQueueSubmit {};
    PFN_vkResetFences                            vkResetFences {};
    PFN_vkResetCommandBuffer                     vkResetCommandBuffer {};
    PFN_vkUnmapMemory                            vkUnmapMemory {};
    PFN_vkUpdateDescriptorSetWithTemplateKHR     vkUpdateDescriptorSetWithTemplateKHR {};
    PFN_vkUpdateDescriptorSets                   vkUpdateDescriptorSets {};
    PFN_vkWaitForFences                          vkWaitForFences {};
    PFN_vkWaitSemaphoresKHR                      vkWaitSemaphoresKHR {};

    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT {};
    PFN_vkSetDebugUtilsObjectTagEXT  vkSetDebugUtilsObjectTagEXT {};
};

template<typename THandle, typename Type = typename THandle::handle_type>
auto ToVector(slice<THandle> handles) -> rstd::vec::Vec<Type> {
    auto res = rstd::vec::Vec<Type>::with_capacity(handles.len());
    for (usize i {}; i < handles.len(); ++i) {
        auto value = *handles[i];
        res.push(rstd::move(value));
    }
    return res;
}

template<typename AllocationType, typename PoolType>
class PoolAllocations {
public:
    PoolAllocations() = default;

    explicit PoolAllocations(rstd::vec::Vec<AllocationType>&& allocations_, VkDevice device_,
                             PoolType pool_, const DeviceDispatch& dld_) noexcept
        : allocations { rstd::move(allocations_) },
          device { device_ },
          pool { pool_ },
          dld { &dld_ } {}

    PoolAllocations(const PoolAllocations&)            = delete;
    PoolAllocations& operator=(const PoolAllocations&) = delete;

    PoolAllocations(PoolAllocations&& rhs) noexcept
        : allocations { rstd::move(rhs.allocations) },
          device { rhs.device },
          pool { rhs.pool },
          dld { rhs.dld } {}

    PoolAllocations& operator=(PoolAllocations&& rhs) noexcept {
        Release();
        allocations = rstd::move(rhs.allocations);
        device      = rhs.device;
        pool        = rhs.pool;
        dld         = rhs.dld;
        return *this;
    }

    ~PoolAllocations() { Release(); }

    usize                 size() const noexcept { return allocations.len(); }
    AllocationType const* data() const noexcept { return allocations.data(); }
    AllocationType        operator[](usize index) const noexcept { return allocations[index]; }
    bool                  IsOutOfPoolMemory() const noexcept { return ! device; }

private:
    void Release() noexcept {
        if (allocations.is_empty()) return;
        const auto allocations_slice =
            slice<AllocationType>::from_raw_parts(allocations.data(), allocations.len());
        VVK_CHECK(Free(device, pool, allocations_slice, *dld));
    }

    rstd::vec::Vec<AllocationType> allocations;
    VkDevice                       device = nullptr;
    PoolType                       pool   = nullptr;
    const DeviceDispatch*          dld    = nullptr;
};

bool Load(InstanceDispatch&) noexcept;
bool Load(VkInstance, InstanceDispatch&) noexcept;
bool Load(VkDevice, InstanceDispatch&) noexcept;
bool Load(VkDevice, DeviceDispatch&) noexcept;

void Destroy(VkInstance, const InstanceDispatch&) noexcept;
void Destroy(VkDevice, const InstanceDispatch&) noexcept;
void Destroy(VkInstance, VkDebugUtilsMessengerEXT, const InstanceDispatch&) noexcept;
void Destroy(VkInstance, VkSurfaceKHR, const InstanceDispatch&) noexcept;
void Destroy(VkDevice, VkCommandPool, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkBuffer, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkPipeline, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkPipelineLayout, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkRenderPass, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDescriptorSetLayout, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkImage, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkImageView, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDeviceMemory, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkShaderModule, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSwapchainKHR, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSampler, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSemaphore, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkFence, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkFramebuffer, const DeviceDispatch&) noexcept;

VkResult Free(VkDevice, VkCommandPool, slice<VkCommandBuffer>, const DeviceDispatch&) noexcept;

using DebugUtilsMessenger = Handle<VkDebugUtilsMessengerEXT, VkInstance, InstanceDispatch>;
using DescriptorSetLayout = Handle<VkDescriptorSetLayout, VkDevice, DeviceDispatch>;
using SurfaceKHR          = Handle<VkSurfaceKHR, VkInstance, InstanceDispatch>;
using Pipeline            = Handle<VkPipeline, VkDevice, DeviceDispatch>;
using PipelineLayout      = Handle<VkPipelineLayout, VkDevice, DeviceDispatch>;
using RenderPass          = Handle<VkRenderPass, VkDevice, DeviceDispatch>;
using Sampler             = Handle<VkSampler, VkDevice, DeviceDispatch>;

using DescriptorSets = PoolAllocations<VkDescriptorSet, VkDescriptorPool>;
using CommandBuffers = PoolAllocations<VkCommandBuffer, VkCommandPool>;

} // namespace vvk

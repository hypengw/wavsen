module;
#include "vvk/macros.hpp"
#include <vulkan/vulkan.h>

module wavsen.vvk;
import rstd.log;
import rstd;
import vulkan;

using namespace rstd::prelude;

namespace vvk
{
constexpr auto vk_count(usize value) noexcept -> rstd::uint32_t {
    return rstd::as_cast<rstd::uint32_t>(value);
}

template<typename T>
bool Proc(T& result, const InstanceDispatch& dld, const char* proc_name,
          VkInstance instance = nullptr) noexcept {
    result = reinterpret_cast<T>(dld.vkGetInstanceProcAddr(instance, proc_name));
    return result != nullptr;
}

template<typename T>
void Proc(T& result, const DeviceDispatch& dld, const char* proc_name, VkDevice device) noexcept {
    result = reinterpret_cast<T>(dld.vkGetDeviceProcAddr(device, proc_name));
}

bool Load(InstanceDispatch& dld) noexcept {
    if (dld.vkGetInstanceProcAddr == nullptr) {
        dld.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    }
    if (dld.vkGetInstanceProcAddr == nullptr) return false;
#define X(name) Proc(dld.name, dld, #name)
    return X(vkCreateInstance) && X(vkEnumerateInstanceExtensionProperties) &&
           X(vkEnumerateInstanceLayerProperties);
#undef X
}

bool Load(VkInstance instance, InstanceDispatch& dld) noexcept {
#define X(name) Proc(dld.name, dld, #name, instance)
    X(vkCreateDebugUtilsMessengerEXT);
    X(vkDestroyDebugUtilsMessengerEXT);
    X(vkDestroySurfaceKHR);
    X(vkGetPhysicalDeviceFeatures2);
    X(vkGetPhysicalDeviceProperties2);
    X(vkGetPhysicalDeviceQueueFamilyProperties2);
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    X(vkGetPhysicalDeviceSurfaceFormatsKHR);
    X(vkGetPhysicalDeviceSurfacePresentModesKHR);
    X(vkGetPhysicalDeviceSurfaceSupportKHR);
    X(vkGetSwapchainImagesKHR);
    X(vkQueuePresentKHR);

    return X(vkDestroyInstance) && X(vkCreateDevice) && X(vkDestroyDevice) && X(vkDestroyDevice) &&
           X(vkEnumerateDeviceExtensionProperties) && X(vkEnumeratePhysicalDevices) &&
           X(vkGetDeviceProcAddr) && X(vkGetPhysicalDeviceFeatures2) &&
           X(vkGetPhysicalDeviceFormatProperties) && X(vkGetPhysicalDeviceMemoryProperties) &&
           X(vkGetPhysicalDeviceMemoryProperties2) && X(vkGetPhysicalDeviceProperties) &&
           X(vkGetPhysicalDeviceProperties2) && X(vkGetPhysicalDeviceQueueFamilyProperties) &&
           X(vkGetPhysicalDeviceQueueFamilyProperties2);
#undef X
}

bool Load(VkDevice device, DeviceDispatch& dld) noexcept {
#define X(name) Proc(dld.name, dld, #name, device)
    X(vkAcquireNextImageKHR);
    X(vkAllocateCommandBuffers);
    X(vkAllocateDescriptorSets);
    X(vkAllocateMemory);
    X(vkBeginCommandBuffer);
    X(vkBindBufferMemory);
    X(vkBindImageMemory);
    X(vkBindImageMemory2);
    X(vkCmdBeginQuery);
    X(vkCmdBeginRenderPass);
    X(vkCmdBindDescriptorSets);
    X(vkCmdBindIndexBuffer);
    X(vkCmdBindPipeline);
    X(vkCmdBindVertexBuffers);
    X(vkCmdBlitImage);
    X(vkCmdClearColorImage);
    X(vkCmdClearAttachments);
    X(vkCmdCopyBuffer);
    X(vkCmdCopyBufferToImage);
    X(vkCmdCopyImage);
    X(vkCmdCopyImageToBuffer);
    X(vkCmdDispatch);
    X(vkCmdDraw);
    X(vkCmdDrawIndexed);
    X(vkCmdEndQuery);
    X(vkCmdEndRenderPass);
    X(vkCmdEndDebugUtilsLabelEXT);
    X(vkCmdFillBuffer);
    X(vkCmdPipelineBarrier);
    X(vkCmdPipelineBarrier2);
    X(vkCmdPushConstants);
    X(vkCmdPushDescriptorSetKHR);
    X(vkCmdPushDescriptorSetWithTemplateKHR);
    X(vkCmdSetBlendConstants);
    X(vkCmdSetDepthBias);
    X(vkCmdSetDepthBounds);
    X(vkCmdSetEvent);
    X(vkCmdSetScissor);
    X(vkCmdSetStencilCompareMask);
    X(vkCmdSetStencilReference);
    X(vkCmdSetStencilWriteMask);
    X(vkCmdSetViewport);
    X(vkCmdWaitEvents);
    X(vkCmdSetLineWidth);
    X(vkCmdResolveImage);
    X(vkCreateBuffer);
    X(vkCreateBufferView);
    X(vkCreateCommandPool);
    X(vkCreateComputePipelines);
    X(vkCreateDescriptorPool);
    X(vkCreateDescriptorSetLayout);
    X(vkCreateDescriptorUpdateTemplateKHR);
    X(vkCreateEvent);
    X(vkCreateFence);
    X(vkCreateFramebuffer);
    X(vkCreateGraphicsPipelines);
    X(vkCreateImage);
    X(vkCreateImageView);
    X(vkCreatePipelineLayout);
    X(vkCreateQueryPool);
    X(vkCreateRenderPass);
    X(vkCreateSampler);
    X(vkCreateSemaphore);
    X(vkCreateShaderModule);
    X(vkCreateSwapchainKHR);
    X(vkDestroyBuffer);
    X(vkDestroyBufferView);
    X(vkDestroyCommandPool);
    X(vkDestroyDescriptorPool);
    X(vkDestroyDescriptorSetLayout);
    X(vkDestroyDescriptorUpdateTemplateKHR);
    X(vkDestroyEvent);
    X(vkDestroyFence);
    X(vkDestroyFramebuffer);
    X(vkDestroyImage);
    X(vkDestroyImageView);
    X(vkDestroyPipeline);
    X(vkDestroyPipelineLayout);
    X(vkDestroyQueryPool);
    X(vkDestroyRenderPass);
    X(vkDestroySampler);
    X(vkDestroySemaphore);
    X(vkDestroyShaderModule);
    X(vkDestroySwapchainKHR);
    X(vkDeviceWaitIdle);
    X(vkEndCommandBuffer);
    X(vkFreeCommandBuffers);
    X(vkFreeDescriptorSets);
    X(vkFreeMemory);
    X(vkGetBufferMemoryRequirements2);
    X(vkGetDeviceQueue);
    X(vkGetEventStatus);
    X(vkGetFenceStatus);
    X(vkGetImageMemoryRequirements);
    X(vkGetImageMemoryRequirements2);
    X(vkGetImageSubresourceLayout);
    X(vkGetMemoryFdKHR);
    X(vkGetMemoryFdPropertiesKHR);
    X(vkGetSemaphoreFdKHR);
    X(vkGetImageDrmFormatModifierPropertiesEXT);
    X(vkGetQueryPoolResults);
    X(vkGetPipelineExecutablePropertiesKHR);
    X(vkGetPipelineExecutableStatisticsKHR);
    X(vkGetSemaphoreCounterValueKHR);
    X(vkMapMemory);
    X(vkQueueSubmit);
    X(vkResetCommandBuffer);
    X(vkResetFences);
    X(vkSetDebugUtilsObjectNameEXT);
    X(vkSetDebugUtilsObjectTagEXT);
    X(vkUnmapMemory);
    X(vkUpdateDescriptorSetWithTemplateKHR);
    X(vkUpdateDescriptorSets);
    X(vkWaitForFences);
    X(vkWaitSemaphoresKHR);
#undef X
    return true;
}

void Destroy(VkInstance instance, const InstanceDispatch& dld) noexcept {
    dld.vkDestroyInstance(instance, nullptr);
}

void Destroy(VkDevice device, const InstanceDispatch& dld) noexcept {
    dld.vkDestroyDevice(device, nullptr);
}

void Destroy(VkInstance Instance, VkDebugUtilsMessengerEXT handle,
             const InstanceDispatch& dld) noexcept {
    dld.vkDestroyDebugUtilsMessengerEXT(Instance, handle, nullptr);
}

void Destroy(VkDevice device, VkCommandPool handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyCommandPool(device, handle, nullptr);
}
void Destroy(VkDevice device, VkBuffer handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyBuffer(device, handle, nullptr);
}
void Destroy(VkDevice device, VkPipeline handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyPipeline(device, handle, nullptr);
}
void Destroy(VkDevice device, VkPipelineLayout handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyPipelineLayout(device, handle, nullptr);
}

void Destroy(VkDevice device, VkRenderPass handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyRenderPass(device, handle, nullptr);
}

void Destroy(VkDevice device, VkDescriptorSetLayout handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyDescriptorSetLayout(device, handle, nullptr);
}

void Destroy(VkDevice device, VkImage handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyImage(device, handle, nullptr);
}

void Destroy(VkDevice device, VkImageView handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyImageView(device, handle, nullptr);
}

void Destroy(VkDevice device, VkDeviceMemory handle, const DeviceDispatch& dld) noexcept {
    dld.vkFreeMemory(device, handle, nullptr);
}

void Destroy(VkDevice device, VkShaderModule handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyShaderModule(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSwapchainKHR handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySwapchainKHR(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSampler handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySampler(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSemaphore handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySemaphore(device, handle, nullptr);
}

void Destroy(VkDevice device, VkFence handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyFence(device, handle, nullptr);
}

void Destroy(VkDevice device, VkFramebuffer handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyFramebuffer(device, handle, nullptr);
}

void Destroy(VkInstance instance, VkSurfaceKHR handle, const InstanceDispatch& dld) noexcept {
    dld.vkDestroySurfaceKHR(instance, handle, nullptr);
}

VkResult Free(VkDevice device, VkCommandPool pool, slice<VkCommandBuffer> allos,
              const DeviceDispatch& dld) noexcept {
    dld.vkFreeCommandBuffers(device, pool, vk_count(allos.len()), allos.as_raw_ptr());
    return VK_SUCCESS;
}

VkResult Instance::Create(Instance& inst, const VkApplicationInfo& app_info,
                          slice<const char*> layers, slice<const char*> extensions,
                          InstanceDispatch& dld) noexcept {
    VkInstanceCreateInfo ci {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = vk_count(layers.len()),
        .ppEnabledLayerNames     = layers.as_raw_ptr(),
        .enabledExtensionCount   = vk_count(extensions.len()),
        .ppEnabledExtensionNames = extensions.as_raw_ptr(),
    };

    VkInstance instance;
    VkResult   res = dld.vkCreateInstance(&ci, nullptr, &instance);
    if (res == VK_SUCCESS) {
        if (Proc(dld.vkDestroyInstance, dld, "vkDestroyInstance", instance))
            inst = Instance(instance, dld);
        else
            res = VK_ERROR_INITIALIZATION_FAILED;
    }
    return res;
}

rstd::vec::Vec<PhysicalDevice> Instance::EnumeratePhysicalDevices() const noexcept {
    rstd::uint32_t num;
    VVK_CHECK(dld->vkEnumeratePhysicalDevices(handle, &num, nullptr));
    auto vkphysical_devices = rstd::vec::Vec<VkPhysicalDevice>::with_capacity(usize(num));
    vkphysical_devices.resize(usize(num), VK_NULL_HANDLE);
    VVK_CHECK(dld->vkEnumeratePhysicalDevices(handle, &num, vkphysical_devices.data()));
    auto physical_devices = rstd::vec::Vec<PhysicalDevice>::with_capacity(usize(num));
    for (const auto vkphysical_device : vkphysical_devices) {
        physical_devices.push(PhysicalDevice(vkphysical_device, *dld));
    }
    return physical_devices;
}

DebugUtilsMessenger Instance::CreateDebugUtilsMessenger(
    const VkDebugUtilsMessengerCreateInfoEXT& create_info) const noexcept {
    VkDebugUtilsMessengerEXT object;
    VVK_CHECK(dld->vkCreateDebugUtilsMessengerEXT(handle, &create_info, nullptr, &object));
    return DebugUtilsMessenger(object, handle, *dld);
}
VkResult Device::Create(Device& device, VkPhysicalDevice physical_device,
                        slice<VkDeviceQueueCreateInfo> queues_ci,
                        slice<const char*> enabled_extensions, const void* next,
                        DeviceDispatch& dld, const VkPhysicalDeviceFeatures* enabled_features) {
    const VkDeviceCreateInfo ci {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = next,
        .flags                   = 0,
        .queueCreateInfoCount    = vk_count(queues_ci.len()),
        .pQueueCreateInfos       = queues_ci.as_raw_ptr(),
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = vk_count(enabled_extensions.len()),
        .ppEnabledExtensionNames = enabled_extensions.as_raw_ptr(),
        .pEnabledFeatures        = enabled_features,
    };
    VkDevice vkdevice;
    VkResult res = dld.vkCreateDevice(physical_device, &ci, nullptr, &vkdevice);
    if (res == VK_SUCCESS) {
        Load(vkdevice, dld);
        device = Device(vkdevice, dld);
    }
    return res;
}

Queue Device::GetQueue(rstd::uint32_t family_index) const noexcept {
    VkQueue queue;
    dld->vkGetDeviceQueue(handle, family_index, 0, &queue);
    return Queue(queue, *dld);
}

VkMemoryRequirements Device::GetImageMemoryRequirements(VkImage image) const noexcept {
    VkMemoryRequirements requirements;
    dld->vkGetImageMemoryRequirements(handle, image, &requirements);
    return requirements;
}

VkMemoryRequirements2
Device::GetImageMemoryRequirements2(const VkImageMemoryRequirementsInfo2& info,
                                    void* next_structures) const noexcept {
    VkMemoryRequirements2 requirements {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = next_structures,
    };
    dld->vkGetImageMemoryRequirements2(handle, &info, &requirements);
    return requirements;
}

VkMemoryRequirements Device::GetBufferMemoryRequirements(VkBuffer buffer) const noexcept {
    const VkBufferMemoryRequirementsInfo2 info {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        .pNext  = nullptr,
        .buffer = buffer,
    };
    VkMemoryRequirements2 requirements {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = nullptr,
    };
    dld->vkGetBufferMemoryRequirements2(handle, &info, &requirements);
    return requirements.memoryRequirements;
}

VkSubresourceLayout
Device::GetImageSubresourceLayout(VkImage                   image,
                                  const VkImageSubresource& subresource) const noexcept {
    VkSubresourceLayout layout {};
    dld->vkGetImageSubresourceLayout(handle, image, &subresource, &layout);
    return layout;
}

VkResult Device::GetImageDrmFormatModifierPropertiesEXT(
    VkImage image, VkImageDrmFormatModifierPropertiesEXT* props) const noexcept {
    return dld->vkGetImageDrmFormatModifierPropertiesEXT(handle, image, props);
}

VkResult Device::GetMemoryFdPropertiesKHR(VkExternalMemoryHandleTypeFlagBits handle_type, int fd,
                                          VkMemoryFdPropertiesKHR& properties) const noexcept {
    if (! dld->vkGetMemoryFdPropertiesKHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return dld->vkGetMemoryFdPropertiesKHR(handle, handle_type, fd, &properties);
}

VkResult Device::BindImageMemory2(slice<VkBindImageMemoryInfo> bindings) const noexcept {
    return dld->vkBindImageMemory2(handle, vk_count(bindings.len()), bindings.as_raw_ptr());
}

VkResult Device::AllocateMemory(const VkMemoryAllocateInfo& ai, DeviceMemory& mem) const noexcept {
    VkDeviceMemory memory;
    auto           res = dld->vkAllocateMemory(handle, &ai, nullptr, &memory);
    if (res == VK_SUCCESS) mem = DeviceMemory(memory, handle, *dld);
    return res;
}

VkResult Device::CreateBuffer(const VkBufferCreateInfo& ci, Buffer& buffer) const noexcept {
    VkBuffer vkbuffer;
    auto     res = dld->vkCreateBuffer(handle, &ci, nullptr, &vkbuffer);
    if (res == VK_SUCCESS) buffer = Buffer(vkbuffer, handle, *dld);
    return res;
}

VkResult Device::CreateCommandPool(const VkCommandPoolCreateInfo& ci, CommandPool& pool) const {
    VkCommandPool vkpool;
    VkResult      res = dld->vkCreateCommandPool(handle, &ci, nullptr, &vkpool);
    if (res == VK_SUCCESS) pool = CommandPool(vkpool, handle, *dld);
    return res;
}

VkResult Device::CreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& ci,
                                           DescriptorSetLayout& layout) const noexcept {
    VkDescriptorSetLayout object;
    VkResult              res = dld->vkCreateDescriptorSetLayout(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) layout = DescriptorSetLayout(object, handle, *dld);
    return res;
}

VkResult Device::CreateRenderPass(const VkRenderPassCreateInfo& ci,
                                  RenderPass&                   pass) const noexcept {
    VkRenderPass object;
    VkResult     res = dld->vkCreateRenderPass(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) pass = RenderPass(object, handle, *dld);
    return res;
}

VkResult Device::CreatePipelineLayout(const VkPipelineLayoutCreateInfo& ci,
                                      PipelineLayout&                   layout) const noexcept {
    VkPipelineLayout object;
    VkResult         res = dld->vkCreatePipelineLayout(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) layout = PipelineLayout(object, handle, *dld);
    return res;
}

VkResult Device::CreateSwapchainKHR(const VkSwapchainCreateInfoKHR& ci,
                                    SwapchainKHR&                   surface) const noexcept {
    VkSwapchainKHR object;
    VkResult       res = dld->vkCreateSwapchainKHR(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) surface = SwapchainKHR(object, handle, *dld);
    return res;
}

VkResult Device::CreateShaderModule(const VkShaderModuleCreateInfo& ci,
                                    ShaderModule&                   sm) const noexcept {
    VkShaderModule object;
    VkResult       res = dld->vkCreateShaderModule(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sm = ShaderModule(object, handle, *dld);
    return res;
}

VkResult Device::CreateSemaphore(const VkSemaphoreCreateInfo& ci, Semaphore& sm) const noexcept {
    VkSemaphore object;
    VkResult    res = dld->vkCreateSemaphore(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sm = Semaphore(object, handle, *dld);
    return res;
}

VkResult Device::GetSemaphoreFdKHR(const VkSemaphoreGetFdInfoKHR& gi, int* fd) const noexcept {
    if (! dld->vkGetSemaphoreFdKHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return dld->vkGetSemaphoreFdKHR(handle, &gi, fd);
}

VkResult Device::CreateImage(const VkImageCreateInfo& ci, Image& img) const noexcept {
    VkImage object;

    VkResult res = dld->vkCreateImage(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) img = Image(object, handle, *dld);
    return res;
}

VkResult Device::CreateImageView(const VkImageViewCreateInfo& ci, ImageView& view) const noexcept {
    VkImageView object;
    VkResult    res = dld->vkCreateImageView(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) view = ImageView(object, handle, *dld);
    return res;
}

VkResult Device::CreateFramebuffer(const VkFramebufferCreateInfo& ci,
                                   Framebuffer&                   fb) const noexcept {
    VkFramebuffer object;
    VkResult      res = dld->vkCreateFramebuffer(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) fb = Framebuffer(object, handle, *dld);
    return res;
}

VkResult Device::CreateFence(const VkFenceCreateInfo& ci, Fence& fe) const noexcept {
    VkFence  object;
    VkResult res = dld->vkCreateFence(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) fe = Fence(object, handle, *dld);
    return res;
}

VkResult Device::CreateSampler(const VkSamplerCreateInfo& ci, Sampler& sam) const noexcept {
    VkSampler object;
    VkResult  res = dld->vkCreateSampler(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sam = Sampler(object, handle, *dld);
    return res;
}

VkResult Device::CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& ci,
                                        Pipeline& pipeline) const noexcept {
    VkPipeline object;
    VkResult res = dld->vkCreateGraphicsPipelines(handle, VK_NULL_HANDLE, 1, &ci, nullptr, &object);
    if (res == VK_SUCCESS) pipeline = Pipeline(object, handle, *dld);
    return res;
}

VkResult Device::CreateComputePipeline(const VkComputePipelineCreateInfo& ci,
                                       Pipeline&                          pipeline) const noexcept {
    VkPipeline vkpipeline;
    auto res = dld->vkCreateComputePipelines(handle, VK_NULL_HANDLE, 1, &ci, nullptr, &vkpipeline);
    if (res == VK_SUCCESS) pipeline = Pipeline(vkpipeline, handle, *dld);
    return res;
}

VkResult Buffer::BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept {
    return dld->vkBindBufferMemory(owner, handle, memory, offset);
}

VkResult Image::BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept {
    return dld->vkBindImageMemory(owner, handle, memory, offset);
}

VkResult SwapchainKHR::GetImages(rstd::vec::Vec<VkImage>& images) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetSwapchainImagesKHR(owner, handle, &num, nullptr); res != VK_SUCCESS)
        return res;
    images.resize(usize(num), VK_NULL_HANDLE);
    return dld->vkGetSwapchainImagesKHR(owner, handle, &num, images.data());
}

VkPhysicalDeviceProperties PhysicalDevice::GetProperties() const noexcept {
    VkPhysicalDeviceProperties2 props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = nullptr,
    };
    dld->vkGetPhysicalDeviceProperties2(handle, &props);
    return props.properties;
}

void PhysicalDevice::GetProperties2KHR(VkPhysicalDeviceProperties2KHR& props) const noexcept {
    dld->vkGetPhysicalDeviceProperties2(handle, &props);
}

void PhysicalDevice::GetFeatures2KHR(VkPhysicalDeviceFeatures2KHR& feats) const noexcept {
    dld->vkGetPhysicalDeviceFeatures2(handle, &feats);
}

VkResult PhysicalDevice::EnumerateDeviceExtensionProperties(
    rstd::vec::Vec<VkExtensionProperties>& properties) const {
    rstd::uint32_t num;
    VkResult       res = dld->vkEnumerateDeviceExtensionProperties(handle, nullptr, &num, nullptr);
    if (res != VK_SUCCESS) return res;

    properties.resize(usize(num), VkExtensionProperties {});
    return dld->vkEnumerateDeviceExtensionProperties(handle, nullptr, &num, properties.data());
}

VkResult PhysicalDevice::GetSurfaceSupportKHR(rstd::uint32_t queue_family_index,
                                              VkSurfaceKHR surface, bool& supported) const {
    VkBool32 vksupported;
    VkResult res = dld->vkGetPhysicalDeviceSurfaceSupportKHR(
        handle, queue_family_index, surface, &vksupported);
    if (res == VK_SUCCESS) supported = vksupported;
    return res;
}

VkResult
PhysicalDevice::GetSurfaceCapabilitiesKHR(VkSurfaceKHR              surface,
                                          VkSurfaceCapabilitiesKHR& capabilities) const noexcept {
    return (dld->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(handle, surface, &capabilities));
}

VkResult PhysicalDevice::GetSurfaceFormatsKHR(VkSurfaceKHR                        surface,
                                              rstd::vec::Vec<VkSurfaceFormatKHR>& formats) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetPhysicalDeviceSurfaceFormatsKHR(handle, surface, &num, nullptr);
        res != VK_SUCCESS) {
        return res;
    }
    formats.resize(usize(num), VkSurfaceFormatKHR {});
    return dld->vkGetPhysicalDeviceSurfaceFormatsKHR(handle, surface, &num, formats.data());
}

VkResult PhysicalDevice::GetSurfacePresentModesKHR(VkSurfaceKHR                      surface,
                                                   rstd::vec::Vec<VkPresentModeKHR>& modes) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetPhysicalDeviceSurfacePresentModesKHR(handle, surface, &num, nullptr);
        res != VK_SUCCESS) {
        return res;
    }
    modes.resize(usize(num), VK_PRESENT_MODE_IMMEDIATE_KHR);
    return dld->vkGetPhysicalDeviceSurfacePresentModesKHR(handle, surface, &num, modes.data());
}

VkPhysicalDeviceMemoryProperties2
PhysicalDevice::GetMemoryProperties(void* next_structures) const noexcept {
    VkPhysicalDeviceMemoryProperties2 properties {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    properties.pNext = next_structures;
    dld->vkGetPhysicalDeviceMemoryProperties2(handle, &properties);
    return properties;
}

rstd::vec::Vec<VkQueueFamilyProperties> PhysicalDevice::GetQueueFamilyProperties() const {
    rstd::uint32_t num;
    dld->vkGetPhysicalDeviceQueueFamilyProperties2(handle, &num, nullptr);
    auto properties2 = rstd::vec::Vec<VkQueueFamilyProperties2>::with_capacity(usize(num));
    properties2.resize(
        usize(num),
        VkQueueFamilyProperties2 { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
    dld->vkGetPhysicalDeviceQueueFamilyProperties2(handle, &num, properties2.data());

    auto properties = rstd::vec::Vec<VkQueueFamilyProperties>::with_capacity(usize(num));
    properties.reserve(usize(num));
    for (rstd::uint32_t i = 0; i < num; ++i) {
        auto property = properties2[usize(i)].queueFamilyProperties;
        properties.push(rstd::move(property));
    }
    return properties;
}

VkResult CommandPool::Allocate(usize num_buffers, VkCommandBufferLevel level,
                               CommandBuffers& buffers_) const {
    const VkCommandBufferAllocateInfo ai {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = handle,
        .level              = level,
        .commandBufferCount = vk_count(num_buffers),
    };

    auto buffers = rstd::vec::Vec<VkCommandBuffer>::with_capacity(num_buffers);
    buffers.resize(num_buffers, VK_NULL_HANDLE);
    VkResult res = dld->vkAllocateCommandBuffers(owner, &ai, buffers.data());
    if (res == VK_SUCCESS) buffers_ = CommandBuffers(rstd::move(buffers), owner, handle, *dld);
    return res;
}

VkResult DeviceMemory::GetMemoryFdKHR(int* fd) const {
    // Iteration 1a: memory is exported as a real Linux DMA-BUF so that the
    // FD is importable by any consumer that can parse DRM format modifiers,
    // not just another Vulkan instance on the identical driver build.
    const VkMemoryGetFdInfoKHR get_fd_info {
        .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext      = nullptr,
        .memory     = handle,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    return dld->vkGetMemoryFdKHR(owner, &get_fd_info, fd);
}

rstd::Option<rstd::vec::Vec<VkExtensionProperties>>
EnumerateInstanceExtensionProperties(const InstanceDispatch& dld) {
    rstd::uint32_t num;
    if (dld.vkEnumerateInstanceExtensionProperties(nullptr, &num, nullptr) != VK_SUCCESS) {
        return rstd::None();
    }
    auto properties = rstd::vec::Vec<VkExtensionProperties>::with_capacity(usize(num));
    properties.resize(usize(num), VkExtensionProperties {});
    if (dld.vkEnumerateInstanceExtensionProperties(nullptr, &num, properties.data()) !=
        VK_SUCCESS) {
        return rstd::None();
    }
    return rstd::Some(rstd::move(properties));
}

rstd::Option<rstd::vec::Vec<VkLayerProperties>>
EnumerateInstanceLayerProperties(const InstanceDispatch& dld) {
    rstd::uint32_t num;
    if (dld.vkEnumerateInstanceLayerProperties(&num, nullptr) != VK_SUCCESS) {
        return rstd::None();
    }
    auto properties = rstd::vec::Vec<VkLayerProperties>::with_capacity(usize(num));
    properties.resize(usize(num), VkLayerProperties {});
    if (dld.vkEnumerateInstanceLayerProperties(&num, properties.data()) != VK_SUCCESS) {
        return rstd::None();
    }
    return rstd::Some(rstd::move(properties));
}

// clang-format off
const char* ToString(VkResult result) noexcept {
    #define X(str) case VkResult::VK_##str: return "VK_" #str;
    switch (result) {
        X(SUCCESS)
        X(NOT_READY)
        X(TIMEOUT)
        X(EVENT_SET)
        X(EVENT_RESET)
        X(INCOMPLETE)
        X(ERROR_OUT_OF_HOST_MEMORY)
        X(ERROR_OUT_OF_DEVICE_MEMORY)
        X(ERROR_INITIALIZATION_FAILED)
        X(ERROR_DEVICE_LOST)
        X(ERROR_MEMORY_MAP_FAILED)
        X(ERROR_LAYER_NOT_PRESENT)
        X(ERROR_EXTENSION_NOT_PRESENT)
        X(ERROR_FEATURE_NOT_PRESENT)
        X(ERROR_INCOMPATIBLE_DRIVER)
        X(ERROR_TOO_MANY_OBJECTS)
        X(ERROR_FORMAT_NOT_SUPPORTED)
        X(ERROR_FRAGMENTED_POOL)
        X(ERROR_UNKNOWN)
        // Provided by VK_VERSION_1_1
        X(ERROR_OUT_OF_POOL_MEMORY)
        // Provided by VK_VERSION_1_1
        X(ERROR_INVALID_EXTERNAL_HANDLE)
            
        // Provided by VK_KHR_surface
        X(ERROR_SURFACE_LOST_KHR)
        // Provided by VK_KHR_surface
        X(ERROR_NATIVE_WINDOW_IN_USE_KHR)
        // Provided by VK_EXT_debug_report
        X(ERROR_VALIDATION_FAILED_EXT)
        // Provided by VK_KHR_swapchain
        X(SUBOPTIMAL_KHR)

        default:
        return "VK_RESULT_UNKNOWN";
    }
    #undef X
}

const char* ToString(VkFormat format) noexcept {
    #define X(str) case VkFormat::VK_FORMAT_##str: return "VK_FORMAT_##str";
    switch (format) {
        X(UNDEFINED);
        X(R4G4_UNORM_PACK8);
        X(R4G4B4A4_UNORM_PACK16);
        X(B4G4R4A4_UNORM_PACK16);
        X(R5G6B5_UNORM_PACK16);
        X(B5G6R5_UNORM_PACK16);
        X(R5G5B5A1_UNORM_PACK16);
        X(B5G5R5A1_UNORM_PACK16);
        X(A1R5G5B5_UNORM_PACK16);
        X(R8_UNORM);
        X(R8_SNORM);
        X(R8_USCALED);
        X(R8_SSCALED);
        X(R8_UINT);
        X(R8_SINT);
        X(R8_SRGB);
        X(R8G8_UNORM);
        X(R8G8_SNORM);
        X(R8G8_USCALED);
        X(R8G8_SSCALED);
        X(R8G8_UINT);
        X(R8G8_SINT);
        X(R8G8_SRGB);
        X(R8G8B8_UNORM);
        X(R8G8B8_SNORM);
        X(R8G8B8_USCALED);
        X(R8G8B8_SSCALED);
        X(R8G8B8_UINT);
        X(R8G8B8_SINT);
        X(R8G8B8_SRGB);
        X(B8G8R8_UNORM);
        X(B8G8R8_SNORM);
        X(B8G8R8_USCALED);
        X(B8G8R8_SSCALED);
        X(B8G8R8_UINT);
        X(B8G8R8_SINT);
        X(B8G8R8_SRGB);
        X(R8G8B8A8_UNORM);
        X(R8G8B8A8_SNORM);
        X(R8G8B8A8_USCALED);
        X(R8G8B8A8_SSCALED);
        X(R8G8B8A8_UINT);
        X(R8G8B8A8_SINT);
        X(R8G8B8A8_SRGB);
        X(B8G8R8A8_UNORM);
        X(B8G8R8A8_SNORM);
        X(B8G8R8A8_USCALED);
        X(B8G8R8A8_SSCALED);
        X(B8G8R8A8_UINT);
        X(B8G8R8A8_SINT);
        X(B8G8R8A8_SRGB);
        X(A8B8G8R8_UNORM_PACK32);
        X(A8B8G8R8_SNORM_PACK32);
        X(A8B8G8R8_USCALED_PACK32);
        X(A8B8G8R8_SSCALED_PACK32);
        X(A8B8G8R8_UINT_PACK32);
        X(A8B8G8R8_SINT_PACK32);
        X(A8B8G8R8_SRGB_PACK32);
        X(A2R10G10B10_UNORM_PACK32);
        X(A2R10G10B10_SNORM_PACK32);
        X(A2R10G10B10_USCALED_PACK32);
        X(A2R10G10B10_SSCALED_PACK32);
        X(A2R10G10B10_UINT_PACK32);
        X(A2R10G10B10_SINT_PACK32);
        X(A2B10G10R10_UNORM_PACK32);
        X(A2B10G10R10_SNORM_PACK32);
        X(A2B10G10R10_USCALED_PACK32);
        X(A2B10G10R10_SSCALED_PACK32);
        X(A2B10G10R10_UINT_PACK32);
        X(A2B10G10R10_SINT_PACK32);
        X(R16_UNORM);
        X(R16_SNORM);
        X(R16_USCALED);
        X(R16_SSCALED);
        X(R16_UINT);
        X(R16_SINT);
        X(R16_SFLOAT);
        X(R16G16_UNORM);
        X(R16G16_SNORM);
        X(R16G16_USCALED);
        X(R16G16_SSCALED);
        X(R16G16_UINT);
        X(R16G16_SINT);
        X(R16G16_SFLOAT);
        X(R16G16B16_UNORM);
        X(R16G16B16_SNORM);
        X(R16G16B16_USCALED);
        X(R16G16B16_SSCALED);
        X(R16G16B16_UINT);
        X(R16G16B16_SINT);
        X(R16G16B16_SFLOAT);
        X(R16G16B16A16_UNORM);
        X(R16G16B16A16_SNORM);
        X(R16G16B16A16_USCALED);
        X(R16G16B16A16_SSCALED);
        X(R16G16B16A16_UINT);
        X(R16G16B16A16_SINT);
        X(R16G16B16A16_SFLOAT);
        X(R32_UINT);
        X(R32_SINT);
        X(R32_SFLOAT);
        X(R32G32_UINT);
        X(R32G32_SINT);
        X(R32G32_SFLOAT);
        X(R32G32B32_UINT);
        X(R32G32B32_SINT);
        X(R32G32B32_SFLOAT);
        X(R32G32B32A32_UINT);
        X(R32G32B32A32_SINT);
        X(R32G32B32A32_SFLOAT);
        X(R64_UINT);
        X(R64_SINT);
        X(R64_SFLOAT);
        X(R64G64_UINT);
        X(R64G64_SINT);
        X(R64G64_SFLOAT);
        X(R64G64B64_UINT);
        X(R64G64B64_SINT);
        X(R64G64B64_SFLOAT);
        X(R64G64B64A64_UINT);
        X(R64G64B64A64_SINT);
        X(R64G64B64A64_SFLOAT);
        X(B10G11R11_UFLOAT_PACK32);
        X(E5B9G9R9_UFLOAT_PACK32);
        X(D16_UNORM);
        X(X8_D24_UNORM_PACK32);
        X(D32_SFLOAT);
        X(S8_UINT);
        X(D16_UNORM_S8_UINT);
        X(D24_UNORM_S8_UINT);
        X(D32_SFLOAT_S8_UINT);
        X(BC1_RGB_UNORM_BLOCK);
        X(BC1_RGB_SRGB_BLOCK);
        X(BC1_RGBA_UNORM_BLOCK);
        X(BC1_RGBA_SRGB_BLOCK);
        X(BC2_UNORM_BLOCK);
        X(BC2_SRGB_BLOCK);
        X(BC3_UNORM_BLOCK);
        X(BC3_SRGB_BLOCK);
        X(BC4_UNORM_BLOCK);
        X(BC4_SNORM_BLOCK);
        X(BC5_UNORM_BLOCK);
        X(BC5_SNORM_BLOCK);
        X(BC6H_UFLOAT_BLOCK);
        X(BC6H_SFLOAT_BLOCK);
        X(BC7_UNORM_BLOCK);
        X(BC7_SRGB_BLOCK);
        X(MAX_ENUM);

        default:
        return "VK_FORMAT_UNKNOWN";
    }
    #undef X
}
const char* ToString(VkColorSpaceKHR o) noexcept {
    #define X(str) case VkColorSpaceKHR::VK_##str: return "VK_##str";
    switch (o) {
        X(COLOR_SPACE_SRGB_NONLINEAR_KHR);
        X(COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT);
        X(COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
        X(COLOR_SPACE_DISPLAY_P3_LINEAR_EXT);
        X(COLOR_SPACE_DCI_P3_NONLINEAR_EXT);
        X(COLOR_SPACE_BT709_LINEAR_EXT);
        X(COLOR_SPACE_BT709_NONLINEAR_EXT);
        X(COLOR_SPACE_BT2020_LINEAR_EXT);
        X(COLOR_SPACE_HDR10_ST2084_EXT);
        X(COLOR_SPACE_DOLBYVISION_EXT);
        X(COLOR_SPACE_HDR10_HLG_EXT);
        X(COLOR_SPACE_ADOBERGB_LINEAR_EXT);
        X(COLOR_SPACE_ADOBERGB_NONLINEAR_EXT);
        X(COLOR_SPACE_PASS_THROUGH_EXT);
        X(COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT);
        X(COLOR_SPACE_DISPLAY_NATIVE_AMD);

        default:
        return "VK_COLOR_SPACE_UNKNOWN";
    }
    #undef X
}
// cX(lang-format on

} //X( namespace vvk

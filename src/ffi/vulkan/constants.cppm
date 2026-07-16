module;

#include <vulkan/vulkan.h>
#include <cstdint>

// Capture macros before #undef. Comprehensive Vulkan FFI module — covers
// the symbol surface needed by both wavsen video plumbing and OWE's vvk
// dispatch-table renderer. Macros that resist tokenisation are captured
// in a hidden namespace, #undef'd, and re-exported as constexpr alternates.
namespace _wv_vk
{
inline constexpr std::uint32_t k_VK_TRUE                     = VK_TRUE;
inline constexpr std::uint32_t k_VK_FALSE                    = VK_FALSE;
inline constexpr std::uint32_t k_VK_API_VERSION_1_3          = VK_API_VERSION_1_3;
inline constexpr std::uint32_t k_VK_QUEUE_FAMILY_IGNORED     = VK_QUEUE_FAMILY_IGNORED;
inline constexpr std::uint32_t k_VK_QUEUE_FAMILY_FOREIGN_EXT = VK_QUEUE_FAMILY_FOREIGN_EXT;
inline constexpr std::uint64_t k_VK_WHOLE_SIZE               = VK_WHOLE_SIZE;
inline constexpr std::uint32_t k_VK_API_VERSION_1_1          = VK_API_VERSION_1_1;
inline constexpr std::uint32_t k_VK_REMAINING_ARRAY_LAYERS   = VK_REMAINING_ARRAY_LAYERS;
inline constexpr std::uint32_t k_VK_REMAINING_MIP_LEVELS     = VK_REMAINING_MIP_LEVELS;
inline constexpr std::uint32_t k_VK_SUBPASS_EXTERNAL         = VK_SUBPASS_EXTERNAL;
inline constexpr std::uint32_t k_VK_VERSION_1_1              = VK_VERSION_1_1;
} // namespace _wv_vk

#undef VK_TRUE
#undef VK_FALSE
#undef VK_API_VERSION_1_3
#undef VK_QUEUE_FAMILY_IGNORED
#undef VK_QUEUE_FAMILY_FOREIGN_EXT
#undef VK_WHOLE_SIZE
#undef VK_API_VERSION_1_1
#undef VK_REMAINING_ARRAY_LAYERS
#undef VK_REMAINING_MIP_LEVELS
#undef VK_SUBPASS_EXTERNAL
#undef VK_VERSION_1_1

namespace _wv_vk_ext
{
inline constexpr const char* k_VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME =
    VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME =
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME =
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME =
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME =
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME =
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME =
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME =
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME =
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_DEBUG_UTILS_EXTENSION_NAME =
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_MEMORY_BUDGET_EXTENSION_NAME =
    VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME =
    VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME =
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME =
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME =
    VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME =
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_SWAPCHAIN_EXTENSION_NAME = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME =
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME =
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
} // namespace _wv_vk_ext

#undef VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME
#undef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
#undef VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME
#undef VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME
#undef VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME
#undef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#undef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
#undef VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME
#undef VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME
#undef VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
#undef VK_KHR_SWAPCHAIN_EXTENSION_NAME
#undef VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
#undef VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME

// VK_NULL_HANDLE is `#define VK_NULL_HANDLE 0` — wrap with conversion ops
// so it compares cleanly against both dispatchable (pointer) and non-
// dispatchable (uint64_t) handles.
namespace _wv_vk
{
struct NullHandle {
    constexpr operator std::uint64_t() const noexcept { return 0; }
    template<class T>
    constexpr operator T*() const noexcept {
        return nullptr;
    }
};
} // namespace _wv_vk
#undef VK_NULL_HANDLE
#undef VK_MAKE_VERSION

export module vulkan:constants;

export {
    // ---- captured macros ----

    inline constexpr std::uint32_t VK_TRUE                 = _wv_vk::k_VK_TRUE;
    inline constexpr std::uint32_t VK_FALSE                = _wv_vk::k_VK_FALSE;
    inline constexpr std::uint32_t VK_API_VERSION_1_3      = _wv_vk::k_VK_API_VERSION_1_3;
    inline constexpr std::uint32_t VK_QUEUE_FAMILY_IGNORED = _wv_vk::k_VK_QUEUE_FAMILY_IGNORED;
    inline constexpr std::uint32_t VK_QUEUE_FAMILY_FOREIGN_EXT =
        _wv_vk::k_VK_QUEUE_FAMILY_FOREIGN_EXT;
    inline constexpr std::uint64_t VK_WHOLE_SIZE             = _wv_vk::k_VK_WHOLE_SIZE;
    inline constexpr std::uint32_t VK_API_VERSION_1_1        = _wv_vk::k_VK_API_VERSION_1_1;
    inline constexpr std::uint32_t VK_REMAINING_ARRAY_LAYERS = _wv_vk::k_VK_REMAINING_ARRAY_LAYERS;
    inline constexpr std::uint32_t VK_REMAINING_MIP_LEVELS   = _wv_vk::k_VK_REMAINING_MIP_LEVELS;
    inline constexpr std::uint32_t VK_SUBPASS_EXTERNAL       = _wv_vk::k_VK_SUBPASS_EXTERNAL;
    inline constexpr std::uint32_t VK_VERSION_1_1            = _wv_vk::k_VK_VERSION_1_1;

    inline constexpr const char* VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_DEBUG_UTILS_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_MEMORY_BUDGET_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    inline constexpr const char* VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME =
        _wv_vk_ext::k_VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_SWAPCHAIN_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    inline constexpr const char* VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME =
        _wv_vk_ext::k_VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;

    inline constexpr _wv_vk::NullHandle VK_NULL_HANDLE {};

    inline constexpr std::uint32_t VK_MAKE_VERSION(std::uint32_t major, std::uint32_t minor,
                                                   std::uint32_t patch) {
        return (major << 22) | (minor << 12) | patch;
    }
}

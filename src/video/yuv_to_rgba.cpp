module;

/* convert_drm_prime_ touches a wide slice of Vulkan: external memory FD
 * import, image plane memory binding, DRM modifier image creation. The
 * wavsen::ffi::vulkan module exports only a curated subset; pull the
 * full header into the GMF for the implementation. */
#include <vulkan/vulkan.h>
#include "nv12_to_rgba.spv.h" // generated at build time by glslangValidator

module wavsen.video;

import rstd;
import vulkan;
import :vk_device;
import :yuv_to_rgba;

namespace wavsen::video
{

using namespace rstd::prelude;

/* Push-constant struct mirroring `PC` in shaders/nv12_to_rgba.comp.
 * std140-friendly: padded to 16-byte boundaries. */
struct alignas(16) ShaderPushConstants {
    rstd::uint32_t dst_w;
    rstd::uint32_t dst_h;
    rstd::uint32_t _pad0[2];
    float          m_r[4];
    float          m_g[4];
    float          m_b[4];
    float          offset[4];
};
static_assert(sizeof(ShaderPushConstants) == 80, "PC size mismatch with shader");

ColorMatrix make_color_matrix(ColorSpace cs, ColorRange cr) {
    /* Coefficients are the standard ITU-R rec luma/chroma weights, with
     * the limited-range Y/C scaling baked into the matrix so the shader
     * only needs to subtract the offset and matmul. Reference:
     *   BT.709: Kr=0.2126, Kb=0.0722
     *   BT.601: Kr=0.299,  Kb=0.114
     *   BT.2020 (NCL): Kr=0.2627, Kb=0.0593 (treated identically here —
     *     no PQ / HLG support yet, so non-constant-luma BT.2020 is
     *     close enough for SDR fallback).
     */
    ColorMatrix m {};
    if (cr == ColorRange::Full) {
        /* Full range: y_scale = 1.0, c_scale = 1.0; offsets = (0, -.5, -.5). */
        if (cs == ColorSpace::Bt601) {
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.402f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.34414f;
            m.m_g[2] = -0.71414f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.772f;
            m.m_b[2] = 0.0f;
        } else if (cs == ColorSpace::Bt2020) {
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.4746f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.16455f;
            m.m_g[2] = -0.57135f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.8814f;
            m.m_b[2] = 0.0f;
        } else {
            /* BT.709 default. */
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.5748f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.18732f;
            m.m_g[2] = -0.46812f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.85563f;
            m.m_b[2] = 0.0f;
        }
        m.offset[0] = 0.0f;
        m.offset[1] = -128.0f / 255.0f;
        m.offset[2] = -128.0f / 255.0f;
    } else {
        /* Limited range. y_scale = 255/219; c_scale = 255/224.
         * Pre-bake into matrix coefficients. */
        constexpr float ys  = 255.0f / 219.0f;
        constexpr float cs_ = 255.0f / 224.0f;
        if (cs == ColorSpace::Bt601) {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.402f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.34414f * cs_;
            m.m_g[2] = -0.71414f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.772f * cs_;
            m.m_b[2] = 0.0f;
        } else if (cs == ColorSpace::Bt2020) {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.4746f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.16455f * cs_;
            m.m_g[2] = -0.57135f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.8814f * cs_;
            m.m_b[2] = 0.0f;
        } else {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.5748f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.18732f * cs_;
            m.m_g[2] = -0.46812f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.85563f * cs_;
            m.m_b[2] = 0.0f;
        }
        m.offset[0] = -16.0f / 255.0f;
        m.offset[1] = -128.0f / 255.0f;
        m.offset[2] = -128.0f / 255.0f;
    }
    return m;
}

namespace
{

bool fail(Error* err, ref<str> message) {
    if (err) err->message = rstd::string::String::make(message);
    return false;
}

bool fail(Error* err, rstd::string::String message) {
    if (err) err->message = rstd::move(message);
    return false;
}

const char* vk_result_str(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    default: return "VK_ERROR_?";
    }
}

auto vk_error(ref<str> operation, VkResult result) -> rstd::string::String {
    return rstd::format("{}: {}", operation, vk_result_str(result));
}

rstd::uint32_t pick_memory_type(const vvk::PhysicalDevice& phys, rstd::uint32_t mask,
                                VkMemoryPropertyFlags want) {
    const auto mp = phys.GetMemoryProperties().memoryProperties;
    for (rstd::uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool create_image_2d(const vvk::Device& device, const vvk::PhysicalDevice& phys, VkFormat fmt,
                     rstd::uint32_t w, rstd::uint32_t h, VkImageUsageFlags usage,
                     vvk::Image& out_img, vvk::DeviceMemory& out_mem, Error* err) {
    VkImageCreateInfo ici {};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (VkResult r = device.CreateImage(ici, out_img); r != VK_SUCCESS) {
        fail(err, vk_error("vkCreateImage", r));
        return false;
    }
    const auto     mr = device.GetImageMemoryRequirements(*out_img);
    rstd::uint32_t type =
        pick_memory_type(phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        fail(err, "no DEVICE_LOCAL memory type for plane image");
        return false;
    }
    VkMemoryAllocateInfo mai {};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = type;
    if (VkResult r = device.AllocateMemory(mai, out_mem); r != VK_SUCCESS) {
        fail(err, vk_error("vkAllocateMemory(plane)", r));
        return false;
    }
    if (VkResult r = out_img.BindMemory(*out_mem, 0); r != VK_SUCCESS) {
        fail(err, vk_error("vkBindImageMemory(plane)", r));
        return false;
    }
    return true;
}

bool create_image_view(const vvk::Device& device, VkImage img, VkFormat fmt, vvk::ImageView& out,
                       Error* err) {
    VkImageViewCreateInfo vci {};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = img;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = fmt;
    vci.components       = { VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY };
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (VkResult r = device.CreateImageView(vci, out); r != VK_SUCCESS) {
        fail(err, vk_error("vkCreateImageView", r));
        return false;
    }
    return true;
}

void barrier_image(const vvk::CommandBuffer& cmd, VkImage img, VkAccessFlags src_a,
                   VkAccessFlags dst_a, VkImageLayout old_l, VkImageLayout new_l,
                   VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s,
                   rstd::uint32_t src_qf = VK_QUEUE_FAMILY_IGNORED,
                   rstd::uint32_t dst_qf = VK_QUEUE_FAMILY_IGNORED) {
    VkImageMemoryBarrier b {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask       = src_a;
    b.dstAccessMask       = dst_a;
    b.oldLayout           = old_l;
    b.newLayout           = new_l;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    cmd.PipelineBarrier(src_s, dst_s, 0, b);
}

// sync2 variant: only this can name VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR
// for the cross-queue hazard tracker after vkCmdDecodeVideoKHR.
void barrier_image2(const vvk::CommandBuffer& cmd, VkImage img, VkPipelineStageFlags2 src_s,
                    VkAccessFlags2 src_a, VkPipelineStageFlags2 dst_s, VkAccessFlags2 dst_a,
                    VkImageLayout old_l, VkImageLayout new_l,
                    rstd::uint32_t src_qf = VK_QUEUE_FAMILY_IGNORED,
                    rstd::uint32_t dst_qf = VK_QUEUE_FAMILY_IGNORED) {
    VkImageMemoryBarrier2 b {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = src_s;
    b.srcAccessMask       = src_a;
    b.dstStageMask        = dst_s;
    b.dstAccessMask       = dst_a;
    b.oldLayout           = old_l;
    b.newLayout           = new_l;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo di {};
    di.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers    = &b;
    cmd.PipelineBarrier2(di);
}

bool target_exports_sync_fd(ConvertTarget target) { return target == ConvertTarget::BridgeForeign; }

u64 next_completion_generation() {
    static rstd::sync::atomic::Atomic<u64> next { u64(1) };
    auto value = next.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed);
    if (value == u64()) value = next.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed);
    return value;
}

u64 next_content_revision() {
    static rstd::sync::atomic::Atomic<u64> next { u64(1) };
    auto value = next.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed);
    if (value == u64()) value = next.fetch_add(u64(1), rstd::sync::atomic::Ordering::Relaxed);
    return value;
}

void barrier_dst_to_storage(const vvk::CommandBuffer& cmd, VkImage dst, ConvertTarget target) {
    if (target == ConvertTarget::SampledLocal) {
        barrier_image(cmd,
                      dst,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_ACCESS_SHADER_WRITE_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        return;
    }
    barrier_image(cmd,
                  dst,
                  0,
                  VK_ACCESS_SHADER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void barrier_dst_from_storage(const vvk::CommandBuffer& cmd, VkImage dst, ConvertTarget target,
                              rstd::uint32_t queue_family) {
    if (target == ConvertTarget::SampledLocal) {
        barrier_image(cmd,
                      dst,
                      VK_ACCESS_SHADER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return;
    }
    barrier_image(cmd,
                  dst,
                  VK_ACCESS_SHADER_WRITE_BIT,
                  0,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                  queue_family,
                  VK_QUEUE_FAMILY_FOREIGN_EXT);
}

} // namespace

YuvToRgba::~YuvToRgba() {
    if (device_) (void)device_.WaitIdle();
    if (staging_map_ && staging_mem_) staging_mem_.Unmap();
    (void)completion_timeline_.take();
}

auto YuvToRgba::create(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                       rstd::uint32_t queue_family, VkQueue queue, rstd::uint32_t max_w,
                       rstd::uint32_t max_h) -> Result<rstd::boxed::Box<YuvToRgba>, Error> {
    if (max_w == 0 || max_h == 0) {
        return Err(Error { "YuvToRgba: max_w/max_h must be non-zero" });
    }
    // NV12 chroma is 4:2:0, so plane W/H must be even.
    if (max_w & 1u) ++max_w;
    if (max_h & 1u) ++max_h;
    auto  self = rstd::boxed::Box<YuvToRgba>::make();
    Error err;
    if (! self->init(instance, phys, device, queue_family, queue, max_w, max_h, &err)) {
        return Err(rstd::move(err));
    }
    return Ok(rstd::move(self));
}

bool YuvToRgba::init(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                     rstd::uint32_t queue_family, VkQueue queue, rstd::uint32_t max_w,
                     rstd::uint32_t max_h, Error* err) {
    if (! vvk::Load(instance_dispatch_) || ! vvk::Load(instance, instance_dispatch_)) {
        return fail(err, "YuvToRgba: failed to load instance dispatch");
    }
    device_dispatch_ = vvk::DeviceDispatch { instance_dispatch_ };
    if (! vvk::Load(device, device_dispatch_)) {
        return fail(err, "YuvToRgba: failed to load device dispatch");
    }

    instance_     = vvk::Instance(instance, instance_dispatch_, vvk::borrowed_handle);
    phys_         = vvk::PhysicalDevice(phys, instance_dispatch_);
    device_       = vvk::Device(device, device_dispatch_, vvk::borrowed_handle);
    queue_        = vvk::Queue(queue, device_dispatch_);
    queue_family_ = queue_family;
    max_w_        = max_w;
    max_h_        = max_h;

    if (! device_dispatch_.vkGetSemaphoreFdKHR) {
        return fail(err, "vkGetSemaphoreFdKHR missing");
    }

    // ----- Sampler (linear, clamp-to-edge) -----
    {
        VkSamplerCreateInfo sci {};
        sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod       = 0.0f;
        if (VkResult r = device_.CreateSampler(sci, sampler_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateSampler", r));
    }

    // ----- Y image (R8_UNORM, max_w × max_h) -----
    if (! create_image_2d(device_,
                          phys_,
                          VK_FORMAT_R8_UNORM,
                          max_w_,
                          max_h_,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          y_image_,
                          y_memory_,
                          err))
        return false;
    if (! create_image_view(device_, *y_image_, VK_FORMAT_R8_UNORM, y_view_, err)) return false;

    // ----- UV image (R8G8_UNORM, half resolution) -----
    if (! create_image_2d(device_,
                          phys_,
                          VK_FORMAT_R8G8_UNORM,
                          max_w_ / 2,
                          max_h_ / 2,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          uv_image_,
                          uv_memory_,
                          err))
        return false;
    if (! create_image_view(device_, *uv_image_, VK_FORMAT_R8G8_UNORM, uv_view_, err)) return false;

    // ----- Staging buffer (HOST_VISIBLE|COHERENT, NV12-sized) -----
    {
        const VkDeviceSize nv12_size = VkDeviceSize(max_w_) * max_h_ * 3 / 2;
        staging_size_                = nv12_size;
        VkBufferCreateInfo bci {};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = nv12_size;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (VkResult r = device_.CreateBuffer(bci, staging_buf_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateBuffer(stage)", r));
        const auto     mr   = device_.GetBufferMemoryRequirements(*staging_buf_);
        rstd::uint32_t type = pick_memory_type(phys_,
                                               mr.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX)
            return fail(err, "no HOST_VISIBLE|COHERENT memory type for staging");
        VkMemoryAllocateInfo mai {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = mr.size;
        mai.memoryTypeIndex = type;
        if (VkResult r = device_.AllocateMemory(mai, staging_mem_); r != VK_SUCCESS)
            return fail(err, vk_error("vkAllocateMemory(stage)", r));
        if (VkResult r = staging_buf_.BindMemory(*staging_mem_, 0); r != VK_SUCCESS)
            return fail(err, vk_error("vkBindBufferMemory(stage)", r));
        if (VkResult r = staging_mem_.Map(0, VK_WHOLE_SIZE, &staging_map_); r != VK_SUCCESS)
            return fail(err, vk_error("vkMapMemory(stage)", r));
    }

    // ----- Shader module -----
    {
        VkShaderModuleCreateInfo smi {};
        smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = sizeof(nv12_to_rgba_spv);
        smi.pCode    = reinterpret_cast<const rstd::uint32_t*>(nv12_to_rgba_spv);
        if (VkResult r = device_.CreateShaderModule(smi, shader_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateShaderModule", r));
    }

    // ----- Descriptor set layout (binding 0/1 = sampled, binding 2 = storage)
    // -----
    {
        VkDescriptorSetLayoutBinding bs[3] {};
        bs[0].binding         = 0;
        bs[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bs[0].descriptorCount = 1;
        bs[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bs[1].binding         = 1;
        bs[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bs[1].descriptorCount = 1;
        bs[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bs[2].binding         = 2;
        bs[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bs[2].descriptorCount = 1;
        bs[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo dsli {};
        dsli.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsli.bindingCount = 3;
        dsli.pBindings    = bs;
        if (VkResult r = device_.CreateDescriptorSetLayout(dsli, dsl_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateDescriptorSetLayout", r));
    }

    // ----- Pipeline layout (push constants: dst dims + color matrix) -----
    {
        VkPushConstantRange pcr {};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset     = 0;
        pcr.size       = sizeof(ShaderPushConstants);
        VkPipelineLayoutCreateInfo pli {};
        pli.sType                        = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount               = 1;
        const auto descriptor_set_layout = *dsl_;
        pli.pSetLayouts                  = &descriptor_set_layout;
        pli.pushConstantRangeCount       = 1;
        pli.pPushConstantRanges          = &pcr;
        if (VkResult r = device_.CreatePipelineLayout(pli, pipeline_layout_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreatePipelineLayout", r));
    }

    // ----- Compute pipeline -----
    {
        VkPipelineShaderStageCreateInfo ssi {};
        ssi.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssi.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ssi.module = *shader_;
        ssi.pName  = "main";
        VkComputePipelineCreateInfo cpi {};
        cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage  = ssi;
        cpi.layout = *pipeline_layout_;
        if (VkResult r = device_.CreateComputePipeline(cpi, pipeline_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateComputePipelines", r));
    }

    // ----- Descriptor pool + set -----
    {
        VkDescriptorPoolSize ps[2] {};
        ps[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[0].descriptorCount = 2;
        ps[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ps[1].descriptorCount = 1;
        const vvk::DescriptorDeviceDispatch dispatch {
            .create_pool   = device_dispatch_.vkCreateDescriptorPool,
            .destroy_pool  = device_dispatch_.vkDestroyDescriptorPool,
            .allocate_sets = device_dispatch_.vkAllocateDescriptorSets,
            .update_sets   = device_dispatch_.vkUpdateDescriptorSets,
        };
        auto arena = vvk::DescriptorArenaGeneration::Create(
            *device_, 1, slice<VkDescriptorPoolSize>::from_raw_parts(ps, usize(2)), dispatch);
        if (! arena.created())
            return fail(err, vk_error("vkCreateDescriptorPool", arena.api_result));
        descriptor_arena_ = rstd::move(arena.arena);
        auto allocation   = vvk::DescriptorArenaGeneration::Allocate(*descriptor_arena_, *dsl_);
        if (! allocation.allocated())
            return fail(err, vk_error("vkAllocateDescriptorSets", allocation.api_result));
        descriptor_set_ = rstd::move(allocation.lease);
    }

    // ----- Cmd pool + buffer + per-submit fence + signal semaphore -----
    {
        VkCommandPoolCreateInfo cpi {};
        cpi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = queue_family_;
        if (VkResult r = device_.CreateCommandPool(cpi, cmd_pool_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateCommandPool", r));
        if (VkResult r =
                cmd_pool_.Allocate(usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, command_buffers_);
            r != VK_SUCCESS)
            return fail(err, vk_error("vkAllocateCommandBuffers", r));
        cmd_ = vvk::CommandBuffer(command_buffers_[usize()], device_dispatch_);

        VkFenceCreateInfo fci {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (VkResult r = device_.CreateFence(fci, done_fence_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateFence", r));

        VkExportSemaphoreCreateInfo es {};
        es.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        es.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        VkSemaphoreCreateInfo sci {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sci.pNext = &es;
        if (VkResult r = device_.CreateSemaphore(sci, signal_sem_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateSemaphore(signal)", r));

        const auto generation = next_completion_generation();
        auto       timeline   = vvk::TimelineSemaphoreGeneration::Create(
            *device_,
            vvk::MakeQueueDomain(*device_, *queue_, u32(queue_family_), u32(), generation),
            generation);
        if (! timeline.created()) {
            return fail(err,
                        rstd::format("vkCreateSemaphore(completion): {}",
                                     vk_result_str(timeline.api_result)));
        }
        completion_timeline_ = rstd::move(timeline.generation);
        completion_observer_ = Some(vvk::TimelineCompletionObserver::AdoptVulkan(
            *device_,
            device_dispatch_.vkGetSemaphoreCounterValueKHR,
            device_dispatch_.vkWaitSemaphoresKHR));
        if (completion_timeline_.is_none() || ! (*completion_timeline_)->source().valid() ||
            completion_observer_.is_none() || ! completion_observer_->valid()) {
            return fail(err, "timeline completion observer unavailable");
        }
    }

    // Bindings 0/1 are stable across frames — write them once.
    {
        VkDescriptorImageInfo dii_y {};
        dii_y.sampler     = *sampler_;
        dii_y.imageView   = *y_view_;
        dii_y.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo dii_uv {};
        dii_uv.sampler     = *sampler_;
        dii_uv.imageView   = *uv_view_;
        dii_uv.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(descriptor_set_.clone(),
                               0,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_y, usize(1))) ||
            ! batch.WriteImage(descriptor_set_.clone(),
                               1,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_uv, usize(1))) ||
            ! batch.Commit().committed())
            return fail(err, "failed to update static YUV descriptors");
    }

    return true;
}

int YuvToRgba::convert_nv12_(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                             const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm,
                             ConvertTarget target, Error* err) {
    if (dst == VK_NULL_HANDLE) {
        fail(err, "convert_nv12: dst VkImage null");
        return -1;
    }
    if (dst_w == 0 || dst_h == 0) {
        fail(err, "convert_nv12: dst_w/h zero");
        return -1;
    }
    if ((dst_w & 1u) || (dst_h & 1u)) {
        fail(err, "convert_nv12: dst dims must be even (NV12 chroma)");
        return -1;
    }
    if (dst_w > max_w_ || dst_h > max_h_) {
        fail(err, "convert_nv12: dst exceeds configured max extent");
        return -1;
    }
    const usize want = usize(dst_w) * usize(dst_h) * usize(3) / usize(2);
    if (nv12_size != want) {
        fail(err, "convert_nv12: nv12_size mismatch (expected NV12 layout)");
        return -1;
    }

    /* Wait for prior submit — protects cmd_/staging_/dset_ from races. */
    if (fence_pending_) {
        if (VkResult r = done_fence_.Wait(1'000'000'000ull); r != VK_SUCCESS) {
            fail(err, vk_error("vkWaitForFences", r));
            return -1;
        }
        if (VkResult r = done_fence_.Reset(); r != VK_SUCCESS) {
            fail(err, vk_error("vkResetFences", r));
            return -1;
        }
        fence_pending_ = false;
    }

    /* Copy NV12 bytes into staging. */
    rstd::mem::memcpy(staging_map_, nv12, nv12_size);

    /* Create a transient view for the dst image — fresh each call because
     * the bridge cycles dst handles per slot. The view is destroyed at
     * the next call's fence wait via the deferred queue. */
    vvk::ImageView dst_view;
    {
        VkImageViewCreateInfo vci {};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = dst;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
        vci.components       = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY };
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, dst_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(dst)", r));
            return -1;
        }
    }

    /* Bind dst into descriptor binding 2. */
    {
        VkDescriptorImageInfo dii {};
        dii.imageView   = *dst_view;
        dii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(descriptor_set_.clone(),
                               2,
                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii, usize(1))) ||
            ! batch.Commit().committed()) {
            fail(err, "failed to update destination descriptor");
            return -1;
        }
    }

    /* Reset + record. */
    if (VkResult r = cmd_.Reset(); r != VK_SUCCESS) {
        fail(err, vk_error("vkResetCommandBuffer", r));
        return -1;
    }
    VkCommandBufferBeginInfo bi {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = cmd_.Begin(bi); r != VK_SUCCESS) {
        fail(err, vk_error("vkBeginCommandBuffer", r));
        return -1;
    }

    /* Y plane: UNDEFINED → TRANSFER_DST. */
    barrier_image(cmd_,
                  *y_image_,
                  0,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    /* UV plane: UNDEFINED → TRANSFER_DST. */
    barrier_image(cmd_,
                  *uv_image_,
                  0,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    /* Copy Y from staging[0..W*H]. */
    {
        VkBufferImageCopy bic {};
        bic.bufferOffset                = 0;
        bic.bufferRowLength             = 0;
        bic.bufferImageHeight           = 0;
        bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bic.imageSubresource.layerCount = 1;
        bic.imageOffset                 = { 0, 0, 0 };
        bic.imageExtent                 = { dst_w, dst_h, 1 };
        cmd_.CopyBufferToImage(*staging_buf_, *y_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bic);
    }
    /* Copy UV from staging[W*H..W*H + W*H/2]. */
    {
        VkBufferImageCopy bic {};
        bic.bufferOffset                = VkDeviceSize(dst_w) * dst_h;
        bic.bufferRowLength             = 0;
        bic.bufferImageHeight           = 0;
        bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bic.imageSubresource.layerCount = 1;
        bic.imageOffset                 = { 0, 0, 0 };
        bic.imageExtent                 = { dst_w / 2, dst_h / 2, 1 };
        cmd_.CopyBufferToImage(
            *staging_buf_, *uv_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bic);
    }

    /* Y/UV: TRANSFER_DST → SHADER_READ_ONLY. */
    barrier_image(cmd_,
                  *y_image_,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier_image(cmd_,
                  *uv_image_,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    barrier_dst_to_storage(cmd_, dst, target);

    /* Bind + dispatch. */
    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const VkDescriptorSet descriptor_set = descriptor_set_.handle;
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                            *pipeline_layout_,
                            0,
                            slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
                            {});
    ShaderPushConstants pc {};
    pc.dst_w = dst_w;
    pc.dst_h = dst_h;
    for (int i = 0; i < 3; ++i) {
        pc.m_r[i]    = cm.m_r[i];
        pc.m_g[i]    = cm.m_g[i];
        pc.m_b[i]    = cm.m_b[i];
        pc.offset[i] = cm.offset[i];
    }
    cmd_.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, pc);
    const rstd::uint32_t gx = (dst_w + 7) / 8;
    const rstd::uint32_t gy = (dst_h + 7) / 8;
    cmd_.Dispatch(gx, gy, 1);

    barrier_dst_from_storage(cmd_, dst, target, queue_family_);

    if (VkResult r = cmd_.End(); r != VK_SUCCESS) {
        fail(err, vk_error("vkEndCommandBuffer", r));
        return -1;
    }

    auto next_completion_value = completion_value_ + u64(1);
    if (next_completion_value == u64()) ++next_completion_value;
    VkSemaphore    signal_sems[2] = { (*completion_timeline_)->handle(), *signal_sem_ };
    rstd::uint64_t signal_vals[2] = { next_completion_value.to_primitive(), 0 };
    const auto     signal_count   = target_exports_sync_fd(target) ? 2u : 1u;
    VkTimelineSemaphoreSubmitInfo timeline_info {};
    timeline_info.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.signalSemaphoreValueCount = signal_count;
    timeline_info.pSignalSemaphoreValues    = signal_vals;
    VkSubmitInfo si {};
    si.sType                             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext                             = &timeline_info;
    si.commandBufferCount                = 1;
    const VkCommandBuffer command_buffer = *cmd_;
    si.pCommandBuffers                   = &command_buffer;
    si.signalSemaphoreCount              = signal_count;
    si.pSignalSemaphores                 = signal_sems;
    if (VkResult r = queue_.Submit(si, *done_fence_); r != VK_SUCCESS) {
        fail(err, vk_error("vkQueueSubmit", r));
        return -1;
    }
    fence_pending_ = true;
    publish_submission(dst, dst_w, dst_h, target, next_completion_value);
    last_dst_view_ = rstd::move(dst_view);

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR sgfi {};
        sgfi.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        sgfi.semaphore  = *signal_sem_;
        sgfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (VkResult r = device_.GetSemaphoreFdKHR(sgfi, &sync_fd); r != VK_SUCCESS) {
            fail(err, vk_error("vkGetSemaphoreFdKHR", r));
            return -1;
        }
    }
    return sync_fd;
}

int YuvToRgba::convert_av_vk_frame_(const VkFrameImports& im, VkImage dst, rstd::uint32_t dst_w,
                                    rstd::uint32_t dst_h, const ColorMatrix& cm,
                                    ConvertTarget target, Error* err) {
    if (dst == VK_NULL_HANDLE) {
        fail(err, "convert_av_vk_frame: dst null");
        return -1;
    }
    if (im.y_image == VK_NULL_HANDLE) {
        fail(err, "convert_av_vk_frame: AVVkFrame y_image NULL");
        return -1;
    }
    /* Two layouts the producer can hand us:
     *   - disjoint:           y_image != uv_image, two separate VkImages
     *                         in R8 / R8G8 (or R16 / R16G16 for 10-bit);
     *   - single multi-plane: uv_image == NULL (or == y_image), one
     *                         VkImage with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
     *                         backing both planes — what FFmpeg's vulkan
     *                         video decode produces (AV_VK_FRAME_FLAG_
     *                         DISABLE_MULTIPLANE is silently ignored for
     *                         the DPB by the spec).
     * In single-image mode we sample plane 0 / plane 1 via aspect masks. */
    const bool single_image = (im.uv_image == VK_NULL_HANDLE) || (im.uv_image == im.y_image);
    if ((dst_w & 1u) || (dst_h & 1u)) {
        fail(err, "convert_av_vk_frame: dst dims must be even");
        return -1;
    }
    if (dst_w > max_w_ || dst_h > max_h_) {
        fail(err, "convert_av_vk_frame: dst exceeds configured max extent");
        return -1;
    }

    /* Wait for prior submit before reusing cmd_/dset_ — same protection
     * as convert_nv12; the in-flight last_*_view_ destruction below also
     * relies on this. */
    if (fence_pending_) {
        if (VkResult r = done_fence_.Wait(1'000'000'000ull); r != VK_SUCCESS) {
            fail(err, vk_error("vkWaitForFences", r));
            return -1;
        }
        if (VkResult r = done_fence_.Reset(); r != VK_SUCCESS) {
            fail(err, vk_error("vkResetFences", r));
            return -1;
        }
        fence_pending_ = false;
    }

    /* Build per-call image views aliasing the AVVkFrame planes + dst. */
    vvk::ImageView dst_view;
    vvk::ImageView y_view;
    vvk::ImageView uv_view;

    {
        VkImageViewUsageCreateInfo sampled_usage {};
        sampled_usage.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        sampled_usage.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageViewUsageCreateInfo storage_usage {};
        storage_usage.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        storage_usage.usage = VK_IMAGE_USAGE_STORAGE_BIT;
        VkImageViewCreateInfo vci {};
        vci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.pNext      = &sampled_usage;
        vci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY };

        /* Per-plane formats:
         *   - disjoint 8-bit:  R8_UNORM    / R8G8_UNORM
         *   - disjoint 10-bit: R16_UNORM   / R16G16_UNORM
         *   - single multi-plane (always 8-bit here; 10-bit via plane
         *     view requires R10X6_UNORM_PACK16 / R10X6G10X6_UNORM_2PACK16
         *     and the shader expects 8-bit for now): R8_UNORM / R8G8_UNORM
         *     + VK_IMAGE_ASPECT_PLANE_{0,1}_BIT. */
        VkFormat y_fmt, uv_fmt;
        if (single_image) {
            y_fmt  = VK_FORMAT_R8_UNORM;
            uv_fmt = VK_FORMAT_R8G8_UNORM;
        } else {
            y_fmt  = (im.bit_depth >= 16) ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
            uv_fmt = (im.bit_depth >= 16) ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
        }

        const VkImageAspectFlags y_aspect  = single_image
                                                 ? VkImageAspectFlags(VK_IMAGE_ASPECT_PLANE_0_BIT)
                                                 : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
        const VkImageAspectFlags uv_aspect = single_image
                                                 ? VkImageAspectFlags(VK_IMAGE_ASPECT_PLANE_1_BIT)
                                                 : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);

        vci.image            = im.y_image;
        vci.format           = y_fmt;
        vci.subresourceRange = { y_aspect, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, y_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(Y, AVVkFrame)", r));
            return -1;
        }
        vci.image            = single_image ? im.y_image : im.uv_image;
        vci.format           = uv_fmt;
        vci.subresourceRange = { uv_aspect, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, uv_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(UV, AVVkFrame)", r));
            return -1;
        }
        vci.image            = dst;
        vci.pNext            = &storage_usage;
        vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, dst_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(dst, AVVkFrame)", r));
            return -1;
        }
    }

    /* Re-bind all three descriptor slots: Y/UV now alias FFmpeg's
     * images, dst is a fresh slot. */
    {
        VkDescriptorImageInfo      dii_y { *sampler_,
                                           *y_view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_uv { *sampler_,
                                            *uv_view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_d { VK_NULL_HANDLE, *dst_view, VK_IMAGE_LAYOUT_GENERAL };
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(descriptor_set_.clone(),
                               0,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_y, usize(1))) ||
            ! batch.WriteImage(descriptor_set_.clone(),
                               1,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_uv, usize(1))) ||
            ! batch.WriteImage(descriptor_set_.clone(),
                               2,
                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_d, usize(1))) ||
            ! batch.Commit().committed()) {
            fail(err, "failed to update AVVkFrame descriptors");
            return -1;
        }
    }

    if (VkResult r = cmd_.Reset(); r != VK_SUCCESS) {
        fail(err, vk_error("vkResetCommandBuffer", r));
        return -1;
    }
    VkCommandBufferBeginInfo bi {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = cmd_.Begin(bi); r != VK_SUCCESS) {
        fail(err, vk_error("vkBeginCommandBuffer", r));
        return -1;
    }

    /* Acquire Y/UV from FFmpeg's image. FFmpeg's vulkan hwframes pool
     * uses VK_SHARING_MODE_CONCURRENT (we passed multiple queue families
     * to AVVulkanDeviceContext::qf), so AVVkFrame::queue_family[i] is
     * VK_QUEUE_FAMILY_IGNORED and no QFOT is allowed; both indices must
     * be IGNORED then. Only when FFmpeg ran the decode on a real,
     * different family do we record an actual ownership transfer.
     * Synchronization with the prior vkCmdDecodeVideoKHR is established
     * by the timeline semaphore wait at submit; we still set the
     * barrier's srcStageMask to VIDEO_DECODE so the validation layer's
     * per-resource hazard tracker can relate them. Single-image path
     * emits ONE barrier on the shared VkImage (covers all planes). */
    const rstd::uint32_t y_avvk_qf  = *im.y_qf_in_out;
    const rstd::uint32_t uv_avvk_qf = *im.uv_qf_in_out;
    struct QueueTransfer {
        rstd::uint32_t source;
        rstd::uint32_t destination;
    };
    auto qfot_pair = [this](rstd::uint32_t avvk_qf) -> QueueTransfer {
        if (avvk_qf == VK_QUEUE_FAMILY_IGNORED || avvk_qf == queue_family_) {
            return { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED };
        }
        return { avvk_qf, queue_family_ };
    };
    // src side: cross-queue execution dep is the timeline-semaphore wait at
    // submit (dstStage=COMPUTE_SHADER); barrier srcStage must overlap with
    // the wait dstStage so the validator can chain "prior decode → wait →
    // barrier → our compute". VIDEO_DECODE on this queue is meaningless
    // (this is a compute queue) and produces a WAR hazard report.
    // srcAccess=0: the semaphore release covers memory visibility.
    auto [y_acq_src, y_acq_dst]   = qfot_pair(y_avvk_qf);
    auto [uv_acq_src, uv_acq_dst] = qfot_pair(uv_avvk_qf);
    barrier_image2(cmd_,
                   im.y_image,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   0,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   *im.y_layout_in_out,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   y_acq_src,
                   y_acq_dst);
    if (! single_image) {
        barrier_image2(cmd_,
                       im.uv_image,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       0,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       *im.uv_layout_in_out,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       uv_acq_src,
                       uv_acq_dst);
    }

    barrier_dst_to_storage(cmd_, dst, target);

    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const VkDescriptorSet descriptor_set = descriptor_set_.handle;
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                            *pipeline_layout_,
                            0,
                            slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
                            {});
    ShaderPushConstants pc {};
    pc.dst_w = dst_w;
    pc.dst_h = dst_h;
    for (int i = 0; i < 3; ++i) {
        pc.m_r[i]    = cm.m_r[i];
        pc.m_g[i]    = cm.m_g[i];
        pc.m_b[i]    = cm.m_b[i];
        pc.offset[i] = cm.offset[i];
    }
    cmd_.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, pc);
    const rstd::uint32_t gx = (dst_w + 7) / 8;
    const rstd::uint32_t gy = (dst_h + 7) / 8;
    cmd_.Dispatch(gx, gy, 1);

    /* Release Y/UV back in GENERAL layout (FFmpeg's next decode submit
     * expects that), and release dst to FOREIGN for the bridge consumer.
     * Same QFOT rules as ACQUIRE: when AVVkFrame says IGNORED (CONCURRENT
     * pool) we transition layout only and keep both indices IGNORED. */
    QueueTransfer y_release =
        (y_avvk_qf == VK_QUEUE_FAMILY_IGNORED || y_avvk_qf == queue_family_)
            ? QueueTransfer { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED }
            : QueueTransfer { queue_family_, y_avvk_qf };
    QueueTransfer uv_release =
        (uv_avvk_qf == VK_QUEUE_FAMILY_IGNORED || uv_avvk_qf == queue_family_)
            ? QueueTransfer { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED }
            : QueueTransfer { queue_family_, uv_avvk_qf };
    auto [y_rel_src, y_rel_dst]   = y_release;
    auto [uv_rel_src, uv_rel_dst] = uv_release;
    // dst side: VIDEO_DECODE on this queue would be meaningless. The
    // semaphore signal happens after all our cmds (sync1 vkQueueSubmit
    // signals at ALL_COMMANDS implicitly), so dstStage=ALL_COMMANDS,
    // dstAccess=0 just sequences the layout transition before signal.
    barrier_image2(cmd_,
                   im.y_image,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   0,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL,
                   y_rel_src,
                   y_rel_dst);
    if (! single_image) {
        barrier_image2(cmd_,
                       im.uv_image,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       0,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_GENERAL,
                       uv_rel_src,
                       uv_rel_dst);
    }
    barrier_dst_from_storage(cmd_, dst, target, queue_family_);

    if (VkResult r = cmd_.End(); r != VK_SUCCESS) {
        fail(err, vk_error("vkEndCommandBuffer", r));
        return -1;
    }

    /* Wait on AVVkFrame's timeline semaphores at their current values,
     * signal incremented values back. Plus our binary signal_sem_ for
     * the bridge SYNC_FD export. Single-image path uses one shared
     * timeline; main.cpp aliases y/uv to the same sem/value pointer
     * so we deduplicate here to avoid waiting on the same semaphore
     * twice (Vulkan UB). */
    const bool           sem_shared    = (im.y_sem == im.uv_sem);
    const rstd::uint64_t y_wait_val    = *im.y_sem_val_in_out;
    const rstd::uint64_t uv_wait_val   = *im.uv_sem_val_in_out;
    const rstd::uint64_t y_signal_val  = y_wait_val + 1;
    const rstd::uint64_t uv_signal_val = uv_wait_val + 1;

    VkSemaphore          wait_sems[2]   = { im.y_sem, im.uv_sem };
    rstd::uint64_t       wait_vals[2]   = { y_wait_val, uv_wait_val };
    VkPipelineStageFlags wait_stages[2] = {
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
    const rstd::uint32_t wait_count = sem_shared ? 1u : 2u;

    /* Signal FFmpeg timeline semaphore(s), the converter completion
     * timeline, and the bridge binary semaphore when requested. */
    auto next_completion_value = completion_value_ + u64(1);
    if (next_completion_value == u64()) ++next_completion_value;
    VkSemaphore    signal_sems[4] {};
    rstd::uint64_t signal_vals[4] {};
    rstd::uint32_t signal_count = 0;
    signal_sems[signal_count]   = im.y_sem;
    signal_vals[signal_count]   = y_signal_val;
    ++signal_count;
    if (! sem_shared) {
        signal_sems[signal_count] = im.uv_sem;
        signal_vals[signal_count] = uv_signal_val;
        ++signal_count;
    }
    if (target_exports_sync_fd(target)) {
        signal_sems[signal_count] = *signal_sem_;
        signal_vals[signal_count] = 0;
        ++signal_count;
    }
    signal_sems[signal_count] = (*completion_timeline_)->handle();
    signal_vals[signal_count] = next_completion_value.to_primitive();
    ++signal_count;

    VkTimelineSemaphoreSubmitInfo tsi {};
    tsi.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tsi.waitSemaphoreValueCount   = wait_count;
    tsi.pWaitSemaphoreValues      = wait_vals;
    tsi.signalSemaphoreValueCount = signal_count;
    tsi.pSignalSemaphoreValues    = signal_vals;

    VkSubmitInfo si {};
    si.sType                             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext                             = &tsi;
    si.waitSemaphoreCount                = wait_count;
    si.pWaitSemaphores                   = wait_sems;
    si.pWaitDstStageMask                 = wait_stages;
    si.commandBufferCount                = 1;
    const VkCommandBuffer command_buffer = *cmd_;
    si.pCommandBuffers                   = &command_buffer;
    si.signalSemaphoreCount              = signal_count;
    si.pSignalSemaphores                 = signal_sems;
    if (VkResult r = queue_.Submit(si, *done_fence_); r != VK_SUCCESS) {
        fail(err, vk_error("vkQueueSubmit", r));
        return -1;
    }
    fence_pending_ = true;
    publish_submission(dst, dst_w, dst_h, target, next_completion_value);

    /* Update the AVVkFrame's tracked state — caller's contract. */
    *im.y_sem_val_in_out  = y_signal_val;
    *im.uv_sem_val_in_out = uv_signal_val;
    *im.y_layout_in_out   = VK_IMAGE_LAYOUT_GENERAL;
    *im.uv_layout_in_out  = VK_IMAGE_LAYOUT_GENERAL;
    // CONCURRENT pool: both stay IGNORED. Cross-family QFOT: we just
    // released back to the family AVVkFrame had on entry — preserve it.
    *im.y_qf_in_out  = y_avvk_qf;
    *im.uv_qf_in_out = uv_avvk_qf;

    last_dst_view_ = rstd::move(dst_view);
    last_y_view_   = rstd::move(y_view);
    last_uv_view_  = rstd::move(uv_view);

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR sgfi {};
        sgfi.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        sgfi.semaphore  = *signal_sem_;
        sgfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (VkResult r = device_.GetSemaphoreFdKHR(sgfi, &sync_fd); r != VK_SUCCESS) {
            fail(err, vk_error("vkGetSemaphoreFdKHR", r));
            return -1;
        }
    }
    return sync_fd;
}

/* DRM_PRIME zero-copy NV12. Imports the AVDRMFrameDescriptor's dma-buf
 * fds as a disjoint multi-plane VkImage (G8_B8R8_2PLANE_420_UNORM with
 * VK_EXT_image_drm_format_modifier), creates two single-plane R8/R8G8
 * views, dispatches the existing nv12_to_rgba.comp into `dst`. The
 * imported VkImage and its memory objects are deferred-destroyed at the
 * *next* convert_drm_prime call (after the wait on done_fence_) so the
 * GPU has fully consumed them. fds are dup'd for Vulkan to own. */
int YuvToRgba::convert_drm_prime_(const DrmFrameView& drm, VkImage dst, rstd::uint32_t dst_w,
                                  rstd::uint32_t dst_h, const ColorMatrix& cm, ConvertTarget target,
                                  Error* err) {
    if (dst == VK_NULL_HANDLE) {
        fail(err, "convert_drm_prime: dst null");
        return -1;
    }
    if (! device_dispatch_.vkGetMemoryFdPropertiesKHR) {
        fail(err, "convert_drm_prime: vkGetMemoryFdPropertiesKHR missing");
        return -1;
    }
    if (drm.object_count == 0 || drm.layer_count == 0) {
        fail(err, "convert_drm_prime: empty DRM_PRIME descriptor");
        return -1;
    }
    if ((dst_w & 1u) || (dst_h & 1u)) {
        fail(err, "convert_drm_prime: dst dims must be even");
        return -1;
    }
    if (dst_w > max_w_ || dst_h > max_h_) {
        fail(err, "convert_drm_prime: dst exceeds configured max extent");
        return -1;
    }

    /* Wait for prior submit before we destroy last-cycle imports + reuse
     * cmd_/dset_. */
    if (fence_pending_) {
        if (VkResult r = done_fence_.Wait(1'000'000'000ull); r != VK_SUCCESS) {
            fail(err, vk_error("vkWaitForFences", r));
            return -1;
        }
        if (VkResult r = done_fence_.Reset(); r != VK_SUCCESS) {
            fail(err, vk_error("vkResetFences", r));
            return -1;
        }
        fence_pending_ = false;
    }

    last_drm_image_.reset();
    last_drm_memories_.clear();

    /* Map (object_index, plane_index_in_layer) → flat plane index 0/1.
     * Two valid descriptor layouts for NV12:
     *   A) 1 layer, 2 planes  (Y=plane[0], UV=plane[1], potentially same fd)
     *   B) 2 layers, 1 plane each (Y=layers[0].planes[0], UV=layers[1].planes[0])
     * We collapse both to a flat planes[0]=Y, planes[1]=UV. */
    struct FlatPlane {
        rstd::uint32_t object_index;
        rstd::uint64_t offset;
        rstd::uint64_t pitch;
    };
    FlatPlane flat[2] {};
    int       flat_n = 0;
    for (rstd::uint32_t li = 0; li < drm.layer_count && flat_n < 2; ++li) {
        const auto& la = drm.layers[li];
        for (rstd::uint32_t pi = 0; pi < la.plane_count && flat_n < 2; ++pi) {
            flat[flat_n].object_index = la.planes[pi].object_index;
            flat[flat_n].offset       = la.planes[pi].offset;
            flat[flat_n].pitch        = la.planes[pi].pitch;
            ++flat_n;
        }
    }
    if (flat_n != 2) {
        fail(err, rstd::format("convert_drm_prime: expected 2 NV12 planes, got {}", flat_n));
        return -1;
    }

    const rstd::uint64_t modifier = drm.objects[0].format_modifier;

    /* Build the multi-plane VkImage with explicit modifier + per-plane
     * layout. DISJOINT lets us bind a separate VkDeviceMemory per plane
     * — required when planes live in different fds. */
    vvk::DeviceMemory plane_mem[2];
    vvk::Image        drm_image;
    vvk::ImageView    y_view;
    vvk::ImageView    uv_view;
    vvk::ImageView    dst_view;
    bool              disjoint = (flat[0].object_index != flat[1].object_index);

    {
        VkSubresourceLayout pl[2] {};
        pl[0].offset   = flat[0].offset;
        pl[0].rowPitch = flat[0].pitch;
        pl[1].offset   = flat[1].offset;
        pl[1].rowPitch = flat[1].pitch;
        VkImageDrmFormatModifierExplicitCreateInfoEXT mci {};
        mci.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
        mci.drmFormatModifier           = modifier;
        mci.drmFormatModifierPlaneCount = 2;
        mci.pPlaneLayouts               = pl;
        VkExternalMemoryImageCreateInfo emi {};
        emi.sType               = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emi.handleTypes         = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        emi.pNext               = &mci;
        VkFormat view_formats[] = {
            VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
            VK_FORMAT_R8_UNORM,
            VK_FORMAT_R8G8_UNORM,
        };
        VkImageFormatListCreateInfo flci {};
        flci.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        flci.pNext           = &emi;
        flci.viewFormatCount = 3;
        flci.pViewFormats    = view_formats;
        VkImageCreateInfo ici {};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.pNext         = &flci;
        ici.flags         = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        ici.extent        = { drm.width, drm.height, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (disjoint) ici.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
        if (VkResult r = device_.CreateImage(ici, drm_image); r != VK_SUCCESS) {
            fail(err,
                 rstd::format(
                     "vkCreateImage(DRM_PRIME, modifier={}): {}", modifier, vk_result_str(r)));
            return -1;
        }
    }

    /* Per-plane memory import. Vulkan takes ownership of imported fds —
     * dup so the AVFrame can keep its own copy alive for the next pull. */
    auto import_plane =
        [&](int plane_idx, rstd::uint32_t obj_idx, VkImageAspectFlagBits aspect) -> bool {
        const int src_fd    = drm.objects[obj_idx].fd;
        auto      duplicate = rstd::os::fd::BorrowedFd::borrow_raw(src_fd).try_clone_to_owned();
        if (duplicate.is_err()) {
            fail(err,
                 rstd::format("dup(dma_buf) failed: {}",
                              rstd::move(duplicate).unwrap_err_unchecked()));
            return false;
        }
        auto duplicate_fd = rstd::move(duplicate).unwrap_unchecked();
        auto raw_fd       = duplicate_fd.as_raw_fd();

        VkMemoryFdPropertiesKHR fdp {};
        fdp.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
        if (VkResult r = device_.GetMemoryFdPropertiesKHR(
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, raw_fd, fdp);
            r != VK_SUCCESS) {
            fail(err, vk_error("vkGetMemoryFdPropertiesKHR", r));
            return false;
        }

        VkImagePlaneMemoryRequirementsInfo plane_req {};
        plane_req.sType       = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
        plane_req.planeAspect = aspect;
        VkImageMemoryRequirementsInfo2 req_info {};
        req_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
        req_info.image = *drm_image;
        if (disjoint) req_info.pNext = &plane_req;
        const auto req2 = device_.GetImageMemoryRequirements2(req_info);

        const rstd::uint32_t type_bits =
            req2.memoryRequirements.memoryTypeBits & fdp.memoryTypeBits;
        const rstd::uint32_t mtype = pick_memory_type(phys_, type_bits, 0);
        if (mtype == UINT32_MAX) {
            fail(err, "convert_drm_prime: no compatible memory type for imported DMA-BUF");
            return false;
        }

        VkImportMemoryFdInfoKHR ifi {};
        ifi.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        ifi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        ifi.fd         = raw_fd;
        VkMemoryDedicatedAllocateInfo dai {};
        dai.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dai.image = *drm_image;
        ifi.pNext = &dai;
        VkMemoryAllocateInfo mai {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext           = &ifi;
        mai.allocationSize  = drm.objects[obj_idx].size;
        mai.memoryTypeIndex = mtype;
        if (VkResult r = device_.AllocateMemory(mai, plane_mem[plane_idx]); r != VK_SUCCESS) {
            fail(err, vk_error("vkAllocateMemory(import DMA-BUF)", r));
            return false;
        }
        /* fd ownership transfers to Vulkan only after a successful import. */
        (void)rstd::move(duplicate_fd).into_raw_fd();
        return true;
    };

    if (disjoint) {
        if (! import_plane(0, flat[0].object_index, VK_IMAGE_ASPECT_PLANE_0_BIT) ||
            ! import_plane(1, flat[1].object_index, VK_IMAGE_ASPECT_PLANE_1_BIT)) {
            return -1;
        }
        VkBindImagePlaneMemoryInfo bp0 {};
        bp0.sType       = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
        bp0.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
        VkBindImagePlaneMemoryInfo bp1 {};
        bp1.sType       = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
        bp1.planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
        VkBindImageMemoryInfo binds[2] {};
        binds[0].sType        = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        binds[0].pNext        = &bp0;
        binds[0].image        = *drm_image;
        binds[0].memory       = *plane_mem[0];
        binds[0].memoryOffset = 0;
        binds[1].sType        = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        binds[1].pNext        = &bp1;
        binds[1].image        = *drm_image;
        binds[1].memory       = *plane_mem[1];
        binds[1].memoryOffset = 0;
        if (VkResult r = device_.BindImageMemory2(
                slice<VkBindImageMemoryInfo>::from_raw_parts(binds, usize(2)));
            r != VK_SUCCESS) {
            fail(err, vk_error("vkBindImageMemory2(disjoint)", r));
            return -1;
        }
    } else {
        /* Both planes share one fd → one allocation, single bind. */
        if (! import_plane(0, flat[0].object_index, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return -1;
        }
        if (VkResult r = drm_image.BindMemory(*plane_mem[0], 0); r != VK_SUCCESS) {
            fail(err, vk_error("vkBindImageMemory(joint)", r));
            return -1;
        }
    }

    /* Two single-aspect plane views: Y as R8, UV as R8G8. */
    {
        VkImageViewCreateInfo vci {};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.image            = *drm_image;
        vci.components       = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY };
        vci.subresourceRange = { VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1 };
        vci.format           = VK_FORMAT_R8_UNORM;
        if (VkResult r = device_.CreateImageView(vci, y_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(DRM Y)", r));
            return -1;
        }
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        vci.format                      = VK_FORMAT_R8G8_UNORM;
        if (VkResult r = device_.CreateImageView(vci, uv_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(DRM UV)", r));
            return -1;
        }
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.image                       = dst;
        vci.format                      = VK_FORMAT_R8G8B8A8_UNORM;
        if (VkResult r = device_.CreateImageView(vci, dst_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(DRM dst)", r));
            return -1;
        }
    }

    /* Descriptor write — same shape as the AVVkFrame path. */
    {
        VkDescriptorImageInfo      dii_y { *sampler_,
                                           *y_view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_uv { *sampler_,
                                            *uv_view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_d { VK_NULL_HANDLE, *dst_view, VK_IMAGE_LAYOUT_GENERAL };
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(descriptor_set_.clone(),
                               0,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_y, usize(1))) ||
            ! batch.WriteImage(descriptor_set_.clone(),
                               1,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_uv, usize(1))) ||
            ! batch.WriteImage(descriptor_set_.clone(),
                               2,
                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_d, usize(1))) ||
            ! batch.Commit().committed()) {
            fail(err, "failed to update DRM PRIME descriptors");
            return -1;
        }
    }

    if (VkResult r = cmd_.Reset(); r != VK_SUCCESS) {
        fail(err, vk_error("vkResetCommandBuffer", r));
        return -1;
    }
    VkCommandBufferBeginInfo bi {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = cmd_.Begin(bi); r != VK_SUCCESS) {
        fail(err, vk_error("vkBeginCommandBuffer", r));
        return -1;
    }

    /* Acquire imported image from FOREIGN queue family → ours, transition
     * UNDEFINED → SHADER_READ_ONLY (we only read). UNDEFINED is correct
     * because Vulkan must NOT preserve anything across the import — VAAPI
     * already wrote NV12 contents into the dma-buf; the foreign-queue
     * acquire grants us read access. */
    barrier_image(cmd_,
                  *drm_image,
                  0,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_QUEUE_FAMILY_FOREIGN_EXT,
                  queue_family_);
    barrier_dst_to_storage(cmd_, dst, target);

    cmd_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const VkDescriptorSet descriptor_set = descriptor_set_.handle;
    cmd_.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                            *pipeline_layout_,
                            0,
                            slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
                            {});
    ShaderPushConstants pc {};
    pc.dst_w = dst_w;
    pc.dst_h = dst_h;
    for (int i = 0; i < 3; ++i) {
        pc.m_r[i]    = cm.m_r[i];
        pc.m_g[i]    = cm.m_g[i];
        pc.m_b[i]    = cm.m_b[i];
        pc.offset[i] = cm.offset[i];
    }
    cmd_.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, pc);
    const rstd::uint32_t gx = (dst_w + 7) / 8;
    const rstd::uint32_t gy = (dst_h + 7) / 8;
    cmd_.Dispatch(gx, gy, 1);

    /* Release dst to FOREIGN queue family for the bridge consumer. The
     * imported drm_image goes back to FOREIGN too — VAAPI doesn't track
     * Vulkan layouts, but the foreign-queue release is the spec-required
     * way to relinquish ownership of an external resource. */
    barrier_image(cmd_,
                  *drm_image,
                  VK_ACCESS_SHADER_READ_BIT,
                  0,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                  queue_family_,
                  VK_QUEUE_FAMILY_FOREIGN_EXT);
    barrier_dst_from_storage(cmd_, dst, target, queue_family_);

    if (VkResult r = cmd_.End(); r != VK_SUCCESS) {
        fail(err, vk_error("vkEndCommandBuffer", r));
        return -1;
    }

    /* Single binary signal_sem_ for the bridge SYNC_FD export — no
     * wait semaphore: VAAPI submits its own decode work synchronously
     * to the dma-buf via implicit sync; the foreign-queue acquire
     * barrier above is the cross-API sync point. */
    auto next_completion_value = completion_value_ + u64(1);
    if (next_completion_value == u64()) ++next_completion_value;
    VkSemaphore    signal_sems[2] = { (*completion_timeline_)->handle(), *signal_sem_ };
    rstd::uint64_t signal_vals[2] = { next_completion_value.to_primitive(), 0 };
    const auto     signal_count   = target_exports_sync_fd(target) ? 2u : 1u;
    VkTimelineSemaphoreSubmitInfo timeline_info {};
    timeline_info.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.signalSemaphoreValueCount = signal_count;
    timeline_info.pSignalSemaphoreValues    = signal_vals;
    VkSubmitInfo si {};
    si.sType                             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext                             = &timeline_info;
    si.commandBufferCount                = 1;
    const VkCommandBuffer command_buffer = *cmd_;
    si.pCommandBuffers                   = &command_buffer;
    si.signalSemaphoreCount              = signal_count;
    si.pSignalSemaphores                 = signal_sems;
    if (VkResult r = queue_.Submit(si, *done_fence_); r != VK_SUCCESS) {
        fail(err, vk_error("vkQueueSubmit", r));
        return -1;
    }
    fence_pending_ = true;
    publish_submission(dst, dst_w, dst_h, target, next_completion_value);

    last_dst_view_  = rstd::move(dst_view);
    last_y_view_    = rstd::move(y_view);
    last_uv_view_   = rstd::move(uv_view);
    last_drm_image_ = rstd::move(drm_image);
    last_drm_memories_.push(rstd::move(plane_mem[0]));
    if (disjoint) last_drm_memories_.push(rstd::move(plane_mem[1]));

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR sgfi {};
        sgfi.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        sgfi.semaphore  = *signal_sem_;
        sgfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (VkResult r = device_.GetSemaphoreFdKHR(sgfi, &sync_fd); r != VK_SUCCESS) {
            fail(err, vk_error("vkGetSemaphoreFdKHR", r));
            return -1;
        }
    }
    return sync_fd;
}

void YuvToRgba::publish_submission(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                                   ConvertTarget target, u64 completion_value) {
    completion_value_           = completion_value;
    const auto content_revision = next_content_revision();
    last_submission_            = Some(ConversionSubmission {
        .target       = dst,
        .width        = dst_w,
        .height       = dst_h,
        .target_kind  = target,
        .final_layout = target == ConvertTarget::SampledLocal
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL,
        .final_queue_family =
            target == ConvertTarget::SampledLocal ? queue_family_ : VK_QUEUE_FAMILY_FOREIGN_EXT,
        .content_revision = content_revision,
        .readiness        = { (*completion_timeline_)->source(), completion_value },
        .execution_dependency =
            {
                .timeline   = Some((*completion_timeline_).clone()),
                .completion = { (*completion_timeline_)->source(), completion_value },
            },
    });
}

vvk::CompletionObservation YuvToRgba::poll_completion() const noexcept {
    return completion_observer_.is_some() && completion_timeline_.is_some()
               ? completion_observer_->Poll((*completion_timeline_)->source())
               : vvk::CompletionObservation {};
}

Option<vvk::SubmissionToken> YuvToRgba::last_submission_readiness() const noexcept {
    if (last_submission_.is_none()) return None();
    auto readiness = last_submission_->readiness;
    return Some(rstd::move(readiness));
}

vvk::CompletionObservation YuvToRgba::wait_completion(const vvk::SubmissionToken& required,
                                                      u64 timeout_ns) const noexcept {
    return completion_observer_.is_some() ? completion_observer_->Wait(required, timeout_ns)
                                          : vvk::CompletionObservation {};
}

// ---------------------------------------------------------------------------
// Public Result wrappers around the legacy out-param helpers above.
// ---------------------------------------------------------------------------

auto YuvToRgba::convert_nv12(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                             const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm)
    -> Result<int, Error> {
    return convert_nv12(dst, dst_w, dst_h, nv12, nv12_size, cm, ConvertTarget::BridgeForeign);
}

auto YuvToRgba::convert_nv12(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                             const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm,
                             ConvertTarget target) -> Result<int, Error> {
    auto submitted = submit_nv12(dst, dst_w, dst_h, nv12, nv12_size, cm, target);
    if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err());
    return Ok(rstd::move(submitted).unwrap().sync_fd);
}

auto YuvToRgba::submit_nv12(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                            const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm,
                            ConvertTarget target) -> Result<ConversionSubmission, Error> {
    (void)last_submission_.take();
    Error err;
    int   fd = convert_nv12_(dst, dst_w, dst_h, nv12, nv12_size, cm, target, &err);
    if (fd < 0 && ! err.message.is_empty()) return Err(rstd::move(err));
    if (! last_submission_ || ! last_submission_->submitted()) {
        return Err(Error { "YuvToRgba: conversion returned without a submission" });
    }
    auto submission    = last_submission_->clone();
    submission.sync_fd = fd;
    return Ok(rstd::move(submission));
}

auto YuvToRgba::convert_av_vk_frame(const VkFrameImports& imports, VkImage dst,
                                    rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                                    const ColorMatrix& cm) -> Result<int, Error> {
    return convert_av_vk_frame(imports, dst, dst_w, dst_h, cm, ConvertTarget::BridgeForeign);
}

auto YuvToRgba::convert_av_vk_frame(const VkFrameImports& imports, VkImage dst,
                                    rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                                    const ColorMatrix& cm, ConvertTarget target)
    -> Result<int, Error> {
    auto submitted = submit_av_vk_frame(imports, dst, dst_w, dst_h, cm, target);
    if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err());
    return Ok(rstd::move(submitted).unwrap().sync_fd);
}

auto YuvToRgba::submit_av_vk_frame(const VkFrameImports& imports, VkImage dst, rstd::uint32_t dst_w,
                                   rstd::uint32_t dst_h, const ColorMatrix& cm,
                                   ConvertTarget target) -> Result<ConversionSubmission, Error> {
    (void)last_submission_.take();
    Error err;
    int   fd = convert_av_vk_frame_(imports, dst, dst_w, dst_h, cm, target, &err);
    if (fd < 0 && ! err.message.is_empty()) return Err(rstd::move(err));
    if (! last_submission_ || ! last_submission_->submitted()) {
        return Err(Error { "YuvToRgba: conversion returned without a submission" });
    }
    auto submission    = last_submission_->clone();
    submission.sync_fd = fd;
    return Ok(rstd::move(submission));
}

auto YuvToRgba::convert_drm_prime(const DrmFrameView& drm, VkImage dst, rstd::uint32_t dst_w,
                                  rstd::uint32_t dst_h, const ColorMatrix& cm)
    -> Result<int, Error> {
    return convert_drm_prime(drm, dst, dst_w, dst_h, cm, ConvertTarget::BridgeForeign);
}

auto YuvToRgba::convert_drm_prime(const DrmFrameView& drm, VkImage dst, rstd::uint32_t dst_w,
                                  rstd::uint32_t dst_h, const ColorMatrix& cm, ConvertTarget target)
    -> Result<int, Error> {
    auto submitted = submit_drm_prime(drm, dst, dst_w, dst_h, cm, target);
    if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err());
    return Ok(rstd::move(submitted).unwrap().sync_fd);
}

auto YuvToRgba::submit_drm_prime(const DrmFrameView& drm, VkImage dst, rstd::uint32_t dst_w,
                                 rstd::uint32_t dst_h, const ColorMatrix& cm, ConvertTarget target)
    -> Result<ConversionSubmission, Error> {
    (void)last_submission_.take();
    Error err;
    int   fd = convert_drm_prime_(drm, dst, dst_w, dst_h, cm, target, &err);
    if (fd < 0 && ! err.message.is_empty()) return Err(rstd::move(err));
    if (! last_submission_ || ! last_submission_->submitted()) {
        return Err(Error { "YuvToRgba: conversion returned without a submission" });
    }
    auto submission    = last_submission_->clone();
    submission.sync_fd = fd;
    return Ok(rstd::move(submission));
}

} // namespace wavsen::video

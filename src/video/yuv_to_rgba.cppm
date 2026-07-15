export module wavsen.video:yuv_to_rgba;

import rstd;
import vulkan;
import wavsen.vvk;
import :vk_device;     // Error
import :video_decoder; // DrmFrameView

export namespace wavsen::video
{

using namespace rstd::prelude;

// Coefficients for the YUV→RGB push constant. CPU side fills this from
// the source frame's colorspace + range; the shader applies it as
// `rgb = M * (ycbcr + offset)`.
struct ColorMatrix {
    float m_r[3]; // Y, Cb, Cr scalings producing R
    float m_g[3];
    float m_b[3];
    float offset[3]; // subtracted from (Y, Cb, Cr) before matmul
};

// Mirrors FFmpeg's `enum AVColorSpace` for the cases we actually
// branch on. Keeping our own enum avoids leaking <libavutil/pixfmt.h>
// into the public surface.
enum class ColorSpace : u32
{
    Bt709  = 0,
    Bt601  = 1,
    Bt2020 = 2,
};

enum class ColorRange : u32
{
    Limited = 0,
    Full    = 1,
};

enum class ConvertTarget : u32
{
    BridgeForeign = 0,
    SampledLocal  = 1,
};

struct ConversionSubmission {
    VkImage                          target { VK_NULL_HANDLE };
    u32                              width { 0 };
    u32                              height { 0 };
    ConvertTarget                    target_kind { ConvertTarget::BridgeForeign };
    VkImageLayout                    final_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    u32                              final_queue_family { VK_QUEUE_FAMILY_IGNORED };
    u64                              content_revision { 0 };
    vvk::SubmissionToken             readiness;
    vvk::TimelineExecutionDependency execution_dependency;
    int                              sync_fd { -1 };

    bool submitted() const noexcept {
        return target != VK_NULL_HANDLE && width != 0 && height != 0 && content_revision != 0 &&
               readiness.valid() && execution_dependency.valid() &&
               execution_dependency.completion == readiness;
    }

    ConversionSubmission clone() const {
        return ConversionSubmission {
            .target               = target,
            .width                = width,
            .height               = height,
            .target_kind          = target_kind,
            .final_layout         = final_layout,
            .final_queue_family   = final_queue_family,
            .content_revision     = content_revision,
            .readiness            = readiness,
            .execution_dependency = execution_dependency.clone(),
            .sync_fd              = sync_fd,
        };
    }
};

// Derive the ColorMatrix to push to the shader. Defaults to BT.709
// limited when either argument is the canonical "unknown" sentinel.
ColorMatrix make_color_matrix(ColorSpace cs, ColorRange cr);

class YuvToRgba {
public:
    ~YuvToRgba();
    YuvToRgba()                            = default;
    YuvToRgba(const YuvToRgba&)            = delete;
    YuvToRgba& operator=(const YuvToRgba&) = delete;

    static auto create(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                       u32 queue_family, VkQueue queue, u32 max_w, u32 max_h)
        -> Result<rstd::boxed::Box<YuvToRgba>, Error>;

    auto convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const u8* nv12, usize nv12_size,
                      const ColorMatrix& cm) -> rstd::Result<int, Error>;
    auto convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const u8* nv12, usize nv12_size,
                      const ColorMatrix& cm, ConvertTarget target) -> rstd::Result<int, Error>;
    auto submit_nv12(VkImage dst, u32 dst_w, u32 dst_h, const u8* nv12, usize nv12_size,
                     const ColorMatrix& cm, ConvertTarget target)
        -> rstd::Result<ConversionSubmission, Error>;

    struct VkFrameImports {
        VkImage        y_image;
        VkImage        uv_image;
        VkSemaphore    y_sem;
        VkSemaphore    uv_sem;
        u64*           y_sem_val_in_out;
        u64*           uv_sem_val_in_out;
        VkImageLayout* y_layout_in_out;
        VkImageLayout* uv_layout_in_out;
        u32*           y_qf_in_out;
        u32*           uv_qf_in_out;
        u32            src_w;
        u32            src_h;
        // 8 → R8 / R8G8 image views (NV12). 16 → R16 / R16G16 (P010 / P016).
        u32 bit_depth;
    };
    auto convert_av_vk_frame(const VkFrameImports& imports, VkImage dst, u32 dst_w, u32 dst_h,
                             const ColorMatrix& cm) -> rstd::Result<int, Error>;
    auto convert_av_vk_frame(const VkFrameImports& imports, VkImage dst, u32 dst_w, u32 dst_h,
                             const ColorMatrix& cm, ConvertTarget target)
        -> rstd::Result<int, Error>;
    auto submit_av_vk_frame(const VkFrameImports& imports, VkImage dst, u32 dst_w, u32 dst_h,
                            const ColorMatrix& cm, ConvertTarget target)
        -> rstd::Result<ConversionSubmission, Error>;

    /* Zero-copy VAAPI path: imports the DrmFrameView's dma-buf fds as
     * a disjoint multi-plane VkImage (NV12 → R8 + R8G8 plane views),
     * runs the same nv12_to_rgba.comp into `dst`. The transient
     * VkImage / VkDeviceMemory / fd dups live until the *next*
     * convert_drm_prime call returns (cycled via last_drm_*). */
    auto convert_drm_prime(const DrmFrameView& drm, VkImage dst, u32 dst_w, u32 dst_h,
                           const ColorMatrix& cm) -> rstd::Result<int, Error>;
    auto convert_drm_prime(const DrmFrameView& drm, VkImage dst, u32 dst_w, u32 dst_h,
                           const ColorMatrix& cm, ConvertTarget target) -> rstd::Result<int, Error>;
    auto submit_drm_prime(const DrmFrameView& drm, VkImage dst, u32 dst_w, u32 dst_h,
                          const ColorMatrix& cm, ConvertTarget target)
        -> rstd::Result<ConversionSubmission, Error>;

    vvk::CompletionObservation   poll_completion() const noexcept;
    vvk::CompletionObservation   wait_completion(const vvk::SubmissionToken& required,
                                                 u64 timeout_ns) const noexcept;
    Option<vvk::SubmissionToken> last_submission_readiness() const noexcept;

private:
    bool init(VkInstance instance, VkPhysicalDevice phys, VkDevice device, u32 queue_family,
              VkQueue queue, u32 max_w, u32 max_h, Error* err);
    int  convert_nv12_(VkImage dst, u32 dst_w, u32 dst_h, const u8* nv12, usize nv12_size,
                       const ColorMatrix& cm, ConvertTarget target, Error* err);
    int  convert_av_vk_frame_(const VkFrameImports& imports, VkImage dst, u32 dst_w, u32 dst_h,
                              const ColorMatrix& cm, ConvertTarget target, Error* err);
    int  convert_drm_prime_(const DrmFrameView& drm, VkImage dst, u32 dst_w, u32 dst_h,
                            const ColorMatrix& cm, ConvertTarget target, Error* err);
    void publish_submission(VkImage dst, u32 dst_w, u32 dst_h, ConvertTarget target,
                            u64 completion_value);

    vvk::InstanceDispatch instance_dispatch_;
    vvk::DeviceDispatch   device_dispatch_;
    vvk::Instance         instance_;
    vvk::PhysicalDevice   phys_;
    vvk::Device           device_;
    vvk::Queue            queue_;
    u32                   queue_family_ { 0 };

    u32 max_w_ { 0 };
    u32 max_h_ { 0 };

    vvk::ShaderModule        shader_;
    vvk::DescriptorSetLayout dsl_;
    vvk::PipelineLayout      pipeline_layout_;
    vvk::Pipeline            pipeline_;
    vvk::Sampler             sampler_;

    vvk::DeviceMemory y_memory_;
    vvk::Image        y_image_;
    vvk::ImageView    y_view_;

    vvk::DeviceMemory uv_memory_;
    vvk::Image        uv_image_;
    vvk::ImageView    uv_view_;

    vvk::DeviceMemory staging_mem_;
    vvk::Buffer       staging_buf_;
    u8*               staging_map_ { nullptr };
    VkDeviceSize      staging_size_ { 0 };

    vvk::CommandPool    cmd_pool_;
    vvk::CommandBuffers command_buffers_;
    vvk::CommandBuffer  cmd_;

    vvk::Semaphore signal_sem_;
    vvk::Fence     done_fence_;
    bool           fence_pending_ { false };

    u64                                                       completion_value_ { 0 };
    Option<rstd::sync::Arc<vvk::TimelineSemaphoreGeneration>> completion_timeline_;
    Option<vvk::TimelineCompletionObserver>                   completion_observer_;
    Option<ConversionSubmission>                              last_submission_;

    Option<rstd::sync::Arc<vvk::DescriptorArenaGeneration>> descriptor_arena_;
    vvk::DescriptorSetLease                                 descriptor_set_;

    vvk::ImageView last_dst_view_;
    vvk::ImageView last_y_view_;
    vvk::ImageView last_uv_view_;

    /* Cycle of imported DRM-PRIME resources kept alive for one extra
     * frame so the prior submit's GPU work can drain before destroy. */
    rstd::vec::Vec<vvk::DeviceMemory> last_drm_memories_;
    vvk::Image                        last_drm_image_;
};

} // namespace wavsen::video

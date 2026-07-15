export module wavsen.video:video_decoder;

import rstd;
import vulkan;
import :vk_device;

export namespace wavsen::video
{

using namespace rstd::prelude;

struct Nv12Frame {
    rstd::vec::Vec<u8> data;
    u32                width { 0 };
    u32                height { 0 };
    double             pts_seconds { -1.0 };
    u32                colorspace { 0 };
    u32                color_range { 0 };
};

struct ProbeResult {
    u32 width;
    u32 height;
};

enum class NextFrame
{
    Ok,
    Looped,
    Eof,
};

struct VkFrameView {
    VkImage*       img;
    VkImageLayout* layout;
    VkSemaphore*   sem;
    u64*           sem_value;
    u32*           queue_family;
    u32            plane_count;
    u32            width;
    u32            height;
    double         pts_seconds;
    u32            colorspace { 0 };
    u32            color_range { 0 };
    u32            bit_depth { 8 };
};

enum class HwAccel
{
    Auto   = 0,
    Vulkan = 1,
    Vaapi  = 2,
    None   = 3,
};

struct OpenOpts {
    HwAccel              hwaccel { HwAccel::Auto };
    rstd::string::String render_node;
};

struct InputStream {
    using Trait                  = InputStream;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = InputStream;

        auto read(u8* buf, int size) -> int { return rstd::trait_call<0>(this, buf, size); }
        auto seek(i64 offset, int whence) -> i64 {
            return rstd::trait_call<1>(this, offset, whence);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read, &T::seek>;
};

using InputStreamFactory = rstd::boxed::Box<dyn<FnMut<rstd::boxed::Box<dyn<InputStream>>()>>>;

enum class FrameKind
{
    Sw           = 0,
    VulkanShared = 1,
    VaapiDrm     = 2,
};

struct DrmPlane {
    u32 object_index;
    u64 offset;
    u64 pitch;
};

struct DrmLayer {
    u32      fourcc;
    u32      plane_count;
    DrmPlane planes[4];
};

struct DrmObject {
    int fd;
    u64 size;
    u64 format_modifier;
};

struct DrmFrameView {
    u32       object_count { 0 };
    DrmObject objects[4] {};
    u32       layer_count { 0 };
    DrmLayer  layers[4] {};
    u32       width { 0 };
    u32       height { 0 };
    double    pts_seconds { -1.0 };
    u32       colorspace { 0 };
    u32       color_range { 0 };
    u32       bit_depth { 8 };
};

class VideoDecoder {
public:
    static auto probe_native(ref<str> path) -> Result<ProbeResult, Error>;

    static auto open(ref<str> path, u32 target_width, u32 target_height, bool loop)
        -> Result<rstd::boxed::Box<VideoDecoder>, Error>;

    static auto open_with_vk(ref<str> path, u32 target_width, u32 target_height, bool loop,
                             const Producer& producer, const OpenOpts& opts = {})
        -> Result<rstd::boxed::Box<VideoDecoder>, Error>;

    static auto open_from_stream(InputStreamFactory make_stream, u32 target_width,
                                 u32 target_height, bool loop, const Producer* producer = nullptr,
                                 const OpenOpts& opts = {})
        -> Result<rstd::boxed::Box<VideoDecoder>, Error>;

    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    auto next_frame(Nv12Frame& out) -> Result<NextFrame, Error>;
    auto next_vk_frame(VkFrameView& out) -> Result<NextFrame, Error>;
    auto next_drm_frame(DrmFrameView& out) -> Result<NextFrame, Error>;

    FrameKind kind() const { return kind_; }
    bool      using_vk_frames() const { return kind_ == FrameKind::VulkanShared; }
    u32       width() const { return target_width_; }
    u32       height() const { return target_height_; }
    void      set_loop(bool loop) { loop_ = loop; }

    struct State;

    class StateOwner {
    public:
        explicit StateOwner(State* state): state_(state) {}
        ~StateOwner();

        StateOwner(const StateOwner&)            = delete;
        StateOwner& operator=(const StateOwner&) = delete;
        State*      operator->() const { return state_; }
        State&      operator*() const { return *state_; }

    private:
        State* state_;
    };

    explicit VideoDecoder(State* state): state_(state) {}

private:
    struct InputSpec {
        rstd::string::String                       path;
        Option<rstd::boxed::Box<dyn<InputStream>>> stream;
    };

    static auto build_internal(InputSpec input, u32 target_width, u32 target_height, bool loop,
                               void* prebuilt_hwdevice, FrameKind requested_kind, Error* err)
        -> Option<rstd::boxed::Box<VideoDecoder>>;

    int next_frame_(Nv12Frame& out, Error* err);
    int next_vk_frame_(VkFrameView& out, Error* err);
    int next_drm_frame_(DrmFrameView& out, Error* err);

    StateOwner state_;
    u32        target_width_ { 0 };
    u32        target_height_ { 0 };
    bool       loop_ { false };
    FrameKind  kind_ { FrameKind::Sw };
};

} // namespace wavsen::video

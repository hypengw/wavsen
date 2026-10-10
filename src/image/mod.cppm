export module wavsen.image;
export import rstd;

using namespace rstd::prelude;

export namespace wavsen::image
{

using Budget = rstd::alloc::BudgetAllocator<>;

enum class Error
{
    InvalidInput,
    Unsupported,
    ResourceLimit,
    Cancelled,
    Io,
    Decode,
    Encode
};

struct Limits {
    u64 encoded_bytes { 64 * 1024 * 1024 };
    u64 source_pixels { 64 * 1024 * 1024 };
    u64 probe_bytes { 1024 * 1024 };
    u64 operation_ms { 5000 };
    u64 output_bytes { 256 * 1024 * 1024 };
    u64 metadata_bytes { 1024 * 1024 };
};

class Cancellation {
    rstd::sync::Arc<rstd::sync::atomic::Atomic<bool>> state_;
    explicit Cancellation(rstd::sync::Arc<rstd::sync::atomic::Atomic<bool>> state)
        : state_(rstd::move(state)) {}

public:
    static auto make() -> Result<Cancellation, Error>;
    auto        clone() const -> Cancellation { return Cancellation(state_.clone()); }
    void        cancel() const { state_->store(true, rstd::sync::atomic::Ordering::Relaxed); }
    bool        cancelled() const { return state_->load(rstd::sync::atomic::Ordering::Relaxed); }
};

class Pixels {
    struct Storage;
    rstd::sync::Arc<Storage> storage_;
    explicit Pixels(rstd::sync::Arc<Storage>);

public:
    Pixels(Pixels&&) noexcept;
    auto operator=(Pixels&&) noexcept -> Pixels&;
    ~Pixels();
    static auto allocate(Budget, usize bytes, usize alignment = usize(64)) -> Result<Pixels, Error>;
    auto        clone() const -> Pixels;
    auto        data() const -> const rstd::uint8_t*;
    auto        data_mut() -> rstd::uint8_t*;
    auto        size() const -> usize;
};

class Source {
    rstd::io::ReadRange range_;
    String              name_;
    explicit Source(rstd::io::ReadRange range, String name = String::make())
        : range_(rstd::move(range)), name_(rstd::move(name)) {}

public:
    static auto file(ref<rstd::path::Path>) -> Result<Source, Error>;
    static auto memory(Pixels, ref<str> name = {}) -> Result<Source, Error>;
    static auto reader(rstd::io::ReadRange range) -> Source { return Source(rstd::move(range)); }
    auto        clone() const -> Source { return Source(range_.clone(), name_.clone()); }
    auto        name() const -> ref<str> { return name_.as_str(); }
    auto        reader() const -> rstd::io::RangeReader { return range_.reader(); }
    auto        size() const -> u64 { return range_.len(); }
};

struct ImageInfo {
    u32  width;
    u32  height;
    u64  frame_count;
    bool animated {};
};

enum class ResizeMode
{
    Stretch,
    Fit,
    Cover
};

struct DecodeRequest {
    // Zero dimensions keep the source aspect ratio; both zero preserve source size.
    u32        width;
    u32        height;
    ResizeMode mode { ResizeMode::Stretch };
    // Optional output bounds, applied proportionally without enlarging; zero is unbounded.
    u32 max_width;
    u32 max_height;
};

struct DecodedImage {
    Pixels pixels;
    u32    width;
    u32    height;
    u32    stride;
    u64    index;
    u64    duration_us;
    // Rows are packed premultiplied RGBA8; the allocation can include trailing padding.
};

enum class PixelFormat
{
    Gray8,
    GrayAlpha8,
    Rgb8,
    Rgba8,
    Gray16,
    GrayAlpha16,
    Rgb16,
    Rgba16
};
enum class Encoding
{
    Jpeg,
    Png,
    Webp,
    Jxl,
    Other
};

struct MetadataField {
    String name;
    String value;
};

struct ImageDescriptor {
    u32         width;
    u32         height;
    PixelFormat pixels;
    Encoding    encoding;
    u32         orientation { 1 };
    bool        alpha {};
};

struct MetadataPayload {
    String kind;
    Pixels bytes;
};

struct StillImage {
    Pixels               pixels;
    u32                  stride;
    ImageDescriptor      descriptor;
    Option<Pixels>       icc;
    Vec<MetadataField>   metadata = Vec<MetadataField>::make();
    Option<Pixels>       source_exif;
    Option<Pixels>       source_xmp;
    Option<Source>       encoded_source;
    Vec<MetadataPayload> source_metadata = Vec<MetadataPayload>::make();
};

struct Capabilities {
    bool jxl_decode;
    bool jxl_encode;
    bool png_encode;
    u32  ffmpeg_version;
    u32  jxl_version;
    bool jpeg_reconstruction {};
};
auto capabilities() -> Capabilities;

struct EncodeSpec {
    Encoding encoding { Encoding::Jxl };
    u32      effort { 3 };
};

auto encode(const StillImage&, EncodeSpec, Budget, const Cancellation&, Limits = {})
    -> Result<Pixels, Error>;
#if LITO_FEAT_JXL
auto verify_lossless(const StillImage&, const Pixels&, Budget, const Cancellation&, Limits = {})
    -> Result<empty, Error>;

auto recompress_jpeg(const Source&, u32 effort, Budget, const Cancellation&, Limits = {})
    -> Result<Pixels, Error>;
auto reconstruct_jpeg(const Pixels&, Budget, const Cancellation&, Limits = {})
    -> Result<Pixels, Error>;
auto verify_jpeg(const Source&, const Pixels&, Budget, const Cancellation&, Limits = {})
    -> Result<empty, Error>;

enum class Fidelity
{
    SampleLossless,
    JpegReconstruction
};
struct MasterImage {
    Pixels   bytes;
    Fidelity fidelity;
};
auto archive(const StillImage&, Budget, const Cancellation&, Limits = {})
    -> Result<MasterImage, Error>;

#endif

auto exif_snapshot(const StillImage&, Budget) -> Result<Pixels, Error>;
auto preview(const StillImage&, u32 max_width, u32 max_height, Budget, const Cancellation&,
             Limits = {}) -> Result<StillImage, Error>;

#if LITO_FEAT_JXL
struct MetadataPatch {
    Option<slice<u8>> exif;
    Option<slice<u8>> xmp;
};
struct MetadataSnapshot {
    Option<Pixels> exif;
    Option<Pixels> xmp;
};

struct ExtensionId {
    rstd::uint8_t bytes[16];
};
struct ContainerInfo {
    bool  bare_codestream;
    bool  jpeg_reconstruction;
    usize box_count;
};

class JxlContainer {
    struct Impl;
    Box<Impl> impl_;
    explicit JxlContainer(Box<Impl>);

public:
    JxlContainer(JxlContainer&&) noexcept;
    auto operator=(JxlContainer&&) noexcept -> JxlContainer&;
    ~JxlContainer();
    static auto open(Pixels, Limits = {}) -> Result<JxlContainer, Error>;
    auto        codestream(Budget) const -> Result<Pixels, Error>;
    auto        metadata(Budget) const -> Result<MetadataSnapshot, Error>;
    auto        info() const -> ContainerInfo;
    auto        encoded() const -> Pixels;
    auto        extension(ExtensionId, Budget) const -> Result<Option<Pixels>, Error>;
    // An empty payload removes this extension; unrelated boxes stay byte-identical.
    auto rewrite_extension(ExtensionId, slice<u8>, Budget, const Cancellation&) const
        -> Result<Pixels, Error>;
    auto rewrite_metadata(MetadataPatch, Budget, const Cancellation&) const
        -> Result<Pixels, Error>;
};

#endif

class ImageSession {
    struct Impl;
    Box<Impl> impl_;
    explicit ImageSession(Box<Impl>);

public:
    ImageSession(ImageSession&&) noexcept;
    auto operator=(ImageSession&&) noexcept -> ImageSession&;
    ~ImageSession();
    static auto open(Source, Budget, Cancellation, Limits = {}) -> Result<ImageSession, Error>;
    auto        info() const -> ImageInfo;
    auto        next(DecodeRequest = {}) -> Result<Option<DecodedImage>, Error>;
    auto        prepare_still() -> Result<StillImage, Error>;
    // Reopens the same owned source, never caches the complete animation.
    auto reset() -> Result<empty, Error>;
};

} // namespace wavsen::image

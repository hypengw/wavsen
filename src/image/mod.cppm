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
    Decode
};

struct Limits {
    u64 encoded_bytes { 64 * 1024 * 1024 };
    u64 source_pixels { 64 * 1024 * 1024 };
    u64 probe_bytes { 1024 * 1024 };
    u64 operation_ms { 5000 };
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
    // Reopens the same owned source, never caches the complete animation.
    auto reset() -> Result<empty, Error>;
};

} // namespace wavsen::image

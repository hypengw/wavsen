#if LITO_FEAT_IMAGE && LITO_FEAT_JXL
#    include <rstd/macro.hpp>
import wavsen.image;

namespace wavsen::tests::container
{
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace wavsen::image;

auto bytes(const Budget& budget, slice<u8> data) -> Pixels {
    auto result = Pixels::allocate(budget.clone(), data.len()).unwrap();
    rstd::mem::memcpy(result.data_mut(), data.as_raw_ptr(), data.len());
    return result;
}
void equal(const Pixels& a, const Pixels& b) {
    rstd_assert(a.size() == b.size());
    rstd_assert(rstd::mem::memcmp(a.data(), b.data(), a.size()) == 0);
}
int run(int, char**) {
    auto budget = Budget::try_make(usize(8 * 1024 * 1024)).unwrap();
    auto cancel = Cancellation::make().unwrap();
    u8   raw[] { u8(255), u8(10), u8(1), u8(2) };
    auto source = bytes(
        budget, slice<u8>::from_raw_parts(reinterpret_cast<const rstd::byte*>(raw), usize(4)));
    auto container = JxlContainer::open(source.clone()).unwrap();
    auto xmp       = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">one</x:xmpmeta>"_str;
    auto first =
        container.rewrite_metadata({ None(), Some(xmp.as_bytes()) }, budget.clone(), cancel)
            .unwrap();
    auto parsed = JxlContainer::open(first.clone()).unwrap();
    equal(source, parsed.codestream(budget.clone()).unwrap());
    auto second =
        parsed.rewrite_metadata({ None(), Some("two"_str.as_bytes()) }, budget.clone(), cancel)
            .unwrap();
    auto reloaded = JxlContainer::open(second.clone()).unwrap();
    equal(source, reloaded.codestream(budget.clone()).unwrap());
    auto removed =
        reloaded.rewrite_metadata({ None(), Some(slice<u8> {}) }, budget.clone(), cancel).unwrap();
    rstd_assert(removed.size() == usize(44));
    equal(source, JxlContainer::open(removed.clone()).unwrap().codestream(budget.clone()).unwrap());
    // Unknown boxes and extended-length headers remain byte-identical.
    auto extended = Pixels::allocate(budget.clone(), removed.size() + usize(19)).unwrap();
    rstd::mem::memcpy(extended.data_mut(), removed.data(), usize(32));
    const unsigned char box[] = {
        0, 0, 0, 1, 't', 'e', 's', 't', 0, 0, 0, 0, 0, 0, 0, 19, 7, 8, 9
    };
    rstd::mem::memcpy(extended.data_mut() + 32, box, usize(19));
    rstd::mem::memcpy(extended.data_mut() + 51, removed.data() + 32, removed.size() - usize(32));
    auto opaque = JxlContainer::open(extended.clone()).unwrap();
    auto edited =
        opaque.rewrite_metadata({ None(), Some("x"_str.as_bytes()) }, budget.clone(), cancel)
            .unwrap();
    rstd_assert(rstd::mem::memcmp(edited.data() + 41, box, usize(19)) == 0);
    equal(source,
          JxlContainer::open(rstd::move(edited)).unwrap().codestream(budget.clone()).unwrap());
    extended.data_mut()[36] = 'j';
    extended.data_mut()[37] = 'b';
    extended.data_mut()[38] = 'r';
    extended.data_mut()[39] = 'd';
    auto reconstruction     = JxlContainer::open(rstd::move(extended)).unwrap();
    rstd_assert(
        reconstruction.rewrite_metadata({ None(), Some(xmp.as_bytes()) }, budget.clone(), cancel)
            .is_err());
    auto broken =
        bytes(budget,
              slice<u8>::from_raw_parts(reinterpret_cast<const rstd::byte*>(removed.data()),
                                        removed.size() - usize(1)));
    rstd_assert(JxlContainer::open(rstd::move(broken)).is_err());
    removed.data_mut()[32] = 0;
    removed.data_mut()[33] = 0;
    removed.data_mut()[34] = 0;
    removed.data_mut()[35] = 0;
    auto eof               = JxlContainer::open(rstd::move(removed)).unwrap();
    auto eof_edit =
        eof.rewrite_metadata({ None(), Some(xmp.as_bytes()) }, budget.clone(), cancel).unwrap();
    equal(source,
          JxlContainer::open(rstd::move(eof_edit)).unwrap().codestream(budget.clone()).unwrap());
    auto partial = Pixels::allocate(budget.clone(), usize(60)).unwrap();
    rstd::mem::memcpy(partial.data_mut(), first.data(), usize(32));
    unsigned char pieces[] = { 0, 0, 0, 14, 'j', 'x', 'l', 'p', 0,   0, 0, 0, 255, 10,
                               0, 0, 0, 14, 'j', 'x', 'l', 'p', 128, 0, 0, 1, 1,   2 };
    rstd::mem::memcpy(partial.data_mut() + 32, pieces, usize(28));
    auto split = JxlContainer::open(partial.clone()).unwrap();
    equal(source, split.codestream(budget.clone()).unwrap());
    partial.data_mut()[57] = 2;
    rstd_assert(JxlContainer::open(rstd::move(partial)).is_err());
    cancel.cancel();
    rstd_assert(parsed.rewrite_metadata({ None(), Some(xmp.as_bytes()) }, budget.clone(), cancel)
                    .unwrap_err() == Error::Cancelled);
    rstd::io::println("PASS: metadata replacement, removal, opaque boxes, extended length, EOF, "
                      "jxlp, reconstruction and truncation");
    return 0;
}

} // namespace wavsen::tests::container
#endif

#include <wormedit/core/natural_order.h>
#include "text_util.h"

namespace wormedit {
namespace {

auto compare_digits(std::string_view left, std::size_t& li, std::string_view right,
                    std::size_t& ri) noexcept -> int {
    while (li < left.size() && left[li] == '0') ++li;
    while (ri < right.size() && right[ri] == '0') ++ri;
    auto le = li;
    auto re = ri;
    while (le < left.size() && left[le] >= '0' && left[le] <= '9') ++le;
    while (re < right.size() && right[re] >= '0' && right[re] <= '9') ++re;
    const auto llen = le - li;
    const auto rlen = re - ri;
    if (llen != rlen) return llen < rlen ? -1 : 1;
    for (std::size_t offset = 0; offset < llen; ++offset) {
        if (left[li + offset] != right[ri + offset])
            return left[li + offset] < right[ri + offset] ? -1 : 1;
    }
    li = le;
    ri = re;
    return 0;
}


} // namespace

auto natural_name_less(std::string_view left, std::string_view right) noexcept -> bool {
    auto li = std::size_t{};
    auto ri = std::size_t{};
    while (li < left.size() && ri < right.size()) {
        const auto ldigit = left[li] >= '0' && left[li] <= '9';
        const auto rdigit = right[ri] >= '0' && right[ri] <= '9';
        if (ldigit && rdigit) {
            const auto order = compare_digits(left, li, right, ri);
            if (order != 0) return order < 0;
            continue;
        }
        const auto lchar = static_cast<unsigned char>(text_util::ascii_lowercase(left[li]));
        const auto rchar = static_cast<unsigned char>(text_util::ascii_lowercase(right[ri]));
        if (lchar != rchar) return lchar < rchar;
        ++li;
        ++ri;
    }
    if (li != left.size() || ri != right.size()) return li == left.size();
    return text_util::ascii_iequal(left, right) ? left < right : text_util::ascii_iless(left, right);
}

} // namespace wormedit

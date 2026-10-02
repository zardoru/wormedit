#include "ui/nuklear/compose_bookmark_label.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::ui {
namespace {

constexpr float padding = 4.0f;
constexpr std::string_view ellipsis = "\xE2\x80\xA6";

// Length of the UTF-8 sequence starting at `text[index]`, or 0 when malformed.
auto utf8_sequence_length(std::string_view text, std::size_t index) -> std::size_t {
    const auto lead = static_cast<unsigned char>(text[index]);
    const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3
                               : (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || index + length > text.size()) return 0;
    for (std::size_t offset = 1; offset < length; ++offset) {
        if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80) return 0;
    }
    return length;
}

// Byte offsets after each code point, or nullopt when the text is not UTF-8.
auto code_point_ends(std::string_view text) -> std::optional<std::vector<std::size_t>> {
    std::vector<std::size_t> ends;
    for (std::size_t index = 0; index < text.size();) {
        const auto length = utf8_sequence_length(text, index);
        if (length == 0) return std::nullopt;
        index += length;
        ends.push_back(index);
    }
    return ends;
}

auto shorten_to_fit(std::string_view label, const std::vector<std::size_t>& ends,
                    const bookmark_text_width_t& width_of, float maximum_width)
    -> std::optional<std::string> {
    if (width_of(label) <= maximum_width) return std::string(label);
    for (auto kept = ends.size(); kept-- > 0;) {
        const auto prefix = kept == 0 ? std::size_t{0} : ends[kept - 1];
        auto candidate = std::string(label.substr(0, prefix)) + std::string(ellipsis);
        if (width_of(candidate) <= maximum_width) return candidate;
    }
    return std::nullopt;
}

auto overlaps(const bookmark_label_box_t& a, const bookmark_label_box_t& b) -> bool {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

auto layout_bookmark_label(std::string_view label, const bookmark_text_width_t& width_of,
                           float font_height, float line_y, const bookmark_label_field_t& field,
                           const std::optional<bookmark_label_box_t>& previous)
    -> std::optional<bookmark_label_box_t> {
    if (label.empty() || field.w <= 2.0f * padding || font_height <= 0.0f) return std::nullopt;
    const auto ends = code_point_ends(label);
    if (!ends) return std::nullopt;
    const auto text = shorten_to_fit(label, *ends, width_of,
                                     std::min(field.w * 0.5f, field.w - 2.0f * padding));
    if (!text) return std::nullopt;
    const auto width = width_of(*text);
    bookmark_label_box_t box{
        .x = field.x + field.w - padding - width,
        .y = std::clamp(line_y - font_height, field.y,
                        std::max(field.y, field.y + field.h - font_height)),
        .w = width,
        .h = font_height,
        .text = *text,
    };
    if (previous && overlaps(box, *previous)) return std::nullopt;
    return box;
}

} // namespace wormedit::ui

#pragma once
#include "ui/nuklear/text_layout.h"
#include <optional>

namespace wormedit::ui::chrome {
inline constexpr std::string_view text_ellipsis = "…";
struct font_text_width_t {
    const nk_user_font* font;
    auto operator()(std::string_view prefix, std::string_view suffix) const -> float {
        return text_width(font, prefix) + text_width(font, suffix);
    }
};

// No allocation: canvas callers can copy this prefix straight into fixed buffers.
template <typename Width>
inline auto fit_text_prefix(std::string_view text, const Width& width_of, float width,
                            std::string_view suffix = text_ellipsis,
                            std::size_t minimum_prefix = 0) -> std::optional<std::size_t> {
    std::optional<std::size_t> best;
    for (std::size_t end = 0; end < text.size(); ++end) {
        if (end < minimum_prefix || (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U)
            continue;
        if (width_of(text.substr(0, end), suffix) > width) break;
        best = end;
    }
    return best;
}

// Bookmark layout measures the joined string (including kerning) and searches
// from the longest prefix, preserving its existing callback contract.
template <typename Width>
inline auto fit_text_prefix_reverse(std::string_view text, const Width& width_of, float width,
                                    std::string_view suffix = text_ellipsis)
    -> std::optional<std::size_t> {
    for (auto end = text.size(); end-- > 0;) {
        if ((static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) continue;
        if (width_of(text.substr(0, end), suffix) <= width) return end;
    }
    return std::nullopt;
}

// The marker list historically removes trailing continuation bytes after each
// popped byte. Preserve that distinct policy until a separate UTF-8 fix is approved.
inline auto fit_marker_text(const nk_user_font* font, std::string_view text, float width)
    -> std::string {
    if (!font || text_width(font, text) <= width) return std::string(text);
    auto kept = text.size();
    while (kept > 0) {
        --kept;
        while (kept > 0 && (static_cast<unsigned char>(text[kept - 1]) & 0xc0U) == 0x80U) --kept;
        auto candidate = std::string(text.substr(0, kept)) + "...";
        if (text_width(font, candidate) <= width) return candidate;
    }
    return "...";
}

enum class ellipsis_overflow_t { empty, suffix, marker_bytes };
inline auto fit_text_with_ellipsis(const nk_user_font* font, std::string_view text, float width,
                                   ellipsis_overflow_t overflow = ellipsis_overflow_t::empty) -> std::string {
    if (overflow == ellipsis_overflow_t::marker_bytes) return fit_marker_text(font, text, width);
    if (!font || text_width(font, text) <= width) return std::string(text);
    const auto keep = fit_text_prefix(text, font_text_width_t{font}, width);
    if (!keep) return overflow == ellipsis_overflow_t::suffix ? std::string(text_ellipsis) : std::string{};
    return std::string(text.substr(0, *keep)) + std::string(text_ellipsis);
}
} // namespace wormedit::ui::chrome

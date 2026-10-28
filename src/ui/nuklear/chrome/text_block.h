#pragma once
#include "wormedit/nuklear_config.h"
#include <algorithm>
#include <string_view>

namespace wormedit::ui::chrome {
// Static text keeps scrolling available in the containing panel and allocates
// one row per complete UTF-8 line, including in narrow confirmation panels.
struct text_block_t {
    std::string_view text;
    void render(nk_context* context) const {
        const auto& font = *context->style.font;
        const float available = std::max(1.0f, nk_window_get_content_region_size(context).x -
            2 * context->style.text.padding.x);
        auto remaining = text;
        while (!remaining.empty()) {
            const int length = line_length(font, remaining, available);
            nk_layout_row_dynamic(context, font.height + 2 * context->style.text.padding.y, 1);
            nk_text(context, remaining.data(), length, NK_TEXT_LEFT);
            remaining.remove_prefix(static_cast<std::size_t>(length));
            if (!remaining.empty() && remaining.front() == '\n') remaining.remove_prefix(1);
        }
    }
  private:
    // Lines break after the last space or hyphen that fits; a word wider than the row still
    // splits where it overflows. A trailing space stays on its line, so the next one starts clean.
    static auto line_length(const nk_user_font& font, std::string_view text, float available) -> int {
        int length = 0;
        int last_break = 0;
        float width = 0;
        while (length < static_cast<int>(text.size()) && text[length] != '\n') {
            nk_rune rune{};
            const int bytes = std::max(1, nk_utf_decode(text.data() + length, &rune,
                static_cast<int>(text.size()) - length));
            const float next = font.width(font.userdata, font.height, text.data() + length, bytes);
            if (length && width + next > available && text[length] != ' ')
                return last_break > 0 ? last_break : length;
            width += next;
            length += bytes;
            if (text[length - 1] == ' ' || text[length - 1] == '-') last_break = length;
        }
        return length;
    }
};
}

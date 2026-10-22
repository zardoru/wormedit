#include "core/text_util.h"
#pragma once
#include "wormedit/nuklear_config.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::ui::chrome {
inline auto text_width(const nk_user_font* font, std::string_view text) -> float {
    return font->width(font->userdata, font->height, text.data(), static_cast<int>(text.size()));
}

inline auto codepoint_size(std::string_view text, std::size_t offset) -> std::size_t {
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t length = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2
                              : (first & 0xF0) == 0xE0 ? 3
                              : (first & 0xF8) == 0xF0 ? 4 : 1;
    if (length > text.size() - offset) return 1;
    for (std::size_t index = 1; index < length; ++index)
        if (!wormedit::text_util::is_utf8_continuation(text[offset + index])) return 1;
    return length;
}

// The last chunk stays in line so the next word can share its remaining width.
inline void split_long_word(const nk_user_font* font, std::string_view word, float width,
                            std::vector<std::string>& lines, std::string& line) {
    std::size_t begin{};
    for (std::size_t offset = 0; offset < word.size();) {
        const auto next = offset + codepoint_size(word, offset);
        if (offset > begin && text_width(font, word.substr(begin, next - begin)) > width) {
            lines.emplace_back(word.substr(begin, offset - begin));
            begin = offset;
        }
        offset = next;
    }
    line = word.substr(begin);
}

// Dialogs retain their space-delimited paragraphs; only overlong words split.
inline auto wrap_words(const nk_user_font* font, std::string_view text, float width)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string line;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto end = text.find(' ', position);
        const auto word = text.substr(position, end == std::string_view::npos ? end : end - position);
        position = end == std::string_view::npos ? text.size() : end + 1;
        const auto candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (text_width(font, candidate) <= width) {
            line = candidate;
            continue;
        }
        if (!line.empty()) lines.push_back(std::exchange(line, {}));
        split_long_word(font, word, width, lines, line);
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

// Tooltips preserve explicit newlines and the whitespace at a wrap boundary.
inline auto wrap_lines(const nk_user_font* font, std::string_view text, float width)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t offset = 0; offset < text.size();) {
        if (text[offset] == '\n') {
            lines.push_back(std::exchange(line, {}));
            ++offset;
            continue;
        }
        const auto length = codepoint_size(text, offset);
        line.append(text.substr(offset, length));
        offset += length;
        if (text_width(font, line) <= width || line.size() == length) continue;
        const auto break_at = line.find_last_of(" \t", line.size() - length);
        if (break_at != std::string::npos && break_at != 0) {
            lines.push_back(line.substr(0, break_at));
            line.erase(0, break_at);
        } else {
            const auto last = line.substr(line.size() - length);
            line.resize(line.size() - length);
            if (!line.empty()) lines.push_back(std::exchange(line, {}));
            line = last;
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

enum class text_wrap_policy_t { dialog_words, tooltip_lines };
inline auto wrap_text(const nk_user_font* font, std::string_view text, float width,
                      text_wrap_policy_t policy = text_wrap_policy_t::dialog_words)
    -> std::vector<std::string> {
    return policy == text_wrap_policy_t::dialog_words ? wrap_words(font, text, width)
                                                     : wrap_lines(font, text, width);
}
} // namespace wormedit::ui::chrome

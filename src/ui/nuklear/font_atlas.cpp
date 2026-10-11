#include "ui/nuklear/font_atlas.h"
#include "ui/nuklear/sdl_backend.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace wormedit::ui::nuklear {
namespace {
constexpr nk_rune latin_ranges[]{0x20, 0xff, 0x2026, 0x2026, 0x2318, 0x2318, 0};
constexpr nk_rune replacement = 0x25a1;
auto is_latin(nk_rune rune) -> bool {
    return (rune >= 0x20 && rune <= 0xff) || rune == 0x2026 || rune == 0x2318;
}
auto read32(std::span<const unsigned char> bytes, std::size_t offset) -> nk_rune {
    return (static_cast<nk_rune>(bytes[offset]) << 24) |
           (static_cast<nk_rune>(bytes[offset + 1]) << 16) |
           (static_cast<nk_rune>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}
auto read16(std::span<const unsigned char> bytes, std::size_t offset) -> unsigned {
    return (static_cast<unsigned>(bytes[offset]) << 8) | bytes[offset + 1];
}
auto cmap_table(std::span<const unsigned char> bytes) -> std::span<const unsigned char> {
    if (bytes.size() < 12) return {};
    const auto count = read16(bytes, 4);
    if (count > (bytes.size() - 12) / 16) return {};
    std::span<const unsigned char> cmap;
    for (unsigned index = 0; index < count; ++index) {
        const auto table = 12 + index * 16;
        const auto offset = read32(bytes, table + 8), length = read32(bytes, table + 12);
        if (offset > bytes.size() || length > bytes.size() - offset) return {};
        if (read32(bytes, table) == 0x636d6170) cmap = bytes.subspan(offset, length);
    }
    return cmap;
}
auto full_cmap(std::span<const unsigned char> cmap) -> std::span<const unsigned char> {
    if (cmap.size() < 4) return {};
    const auto count = read16(cmap, 2);
    if (count > (cmap.size() - 4) / 8) return {};
    for (unsigned index = 0; index < count; ++index) {
        const auto offset = read32(cmap, 4 + index * 8 + 4);
        if (offset > cmap.size() || cmap.size() - offset < 16) continue;
        if (read16(cmap, offset) != 12) continue;
        const auto length = read32(cmap, offset + 4);
        if (length >= 16 && length <= cmap.size() - offset) return cmap.subspan(offset, length);
    }
    return {};
}
}

auto editor_font_t::coverage(std::span<const unsigned char> bytes)
    -> std::vector<std::pair<nk_rune, nk_rune>> {
    const auto cmap = full_cmap(cmap_table(bytes));
    if (cmap.empty()) return {};
    const auto count = read32(cmap, 12);
    if (count > (cmap.size() - 16) / 12) return {};
    std::vector<std::pair<nk_rune, nk_rune>> result;
    for (nk_rune index = 0; index < count; ++index) {
        const auto offset = 16 + index * 12;
        auto first = read32(cmap, offset);
        const auto last = read32(cmap, offset + 4);
        if (first > last || last > 0x10ffff || (!result.empty() && first <= result.back().second))
            return {};
        if (read32(cmap, offset + 8) == 0) ++first;
        if (first <= last) result.emplace_back(first, last);
    }
    return result;
}

void editor_font_t::warn(const char* reason) {
    if (warned_) return;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Editor font: %s; unavailable glyphs use a box.", reason);
    warned_ = true;
}

void editor_font_t::initialize(nk_context* context, const std::filesystem::path& directory) {
    reset();
    directory_ = directory;
    std::ifstream input(directory / "NotoSansCJKjp-Regular.otf", std::ios::binary);
    fallback_data_.assign(std::istreambuf_iterator<char>{input}, {});
    coverage_ = coverage(fallback_data_);
    if (coverage_.empty()) {
        fallback_data_.clear();
        warn("bundled CJK font is missing or invalid");
    } else {
        codepoints_.insert(replacement);
    }
    handle_ = {.userdata = nk_handle_ptr(this), .height = 15.0f, .width = width, .query = query, .texture = {}};
    rebuild(context);
}

void editor_font_t::reset() {
    baked_ = nullptr;
    handle_ = {};
    directory_.clear();
    fallback_data_.clear();
    coverage_.clear();
    codepoints_.clear();
    ranges_.clear();
    glyphs_.clear();
    dirty_ = warned_ = limited_ = false;
}

auto editor_font_t::supported(nk_rune rune) const -> bool {
    const auto found = std::ranges::lower_bound(coverage_, rune, {},
        [](const auto& range) { return range.second; });
    return found != coverage_.end() && found->first <= rune;
}

void editor_font_t::observe(std::string_view text) {
    while (!text.empty()) {
        nk_rune rune{};
        const auto length = nk_utf_decode(text.data(), &rune, static_cast<int>(text.size()));
        if (length <= 0) break;
        text.remove_prefix(static_cast<std::size_t>(length));
        if (is_latin(rune) || rune < 0x20 || codepoints_.contains(rune)) continue;
        if (!supported(rune)) { warn("text contains an unsupported character"); continue; }
        if (codepoints_.size() >= glyph_limit) {
            limited_ = true;
            warn("4096-character CJK atlas limit reached");
            continue;
        }
        codepoints_.insert(rune);
        dirty_ = true;
    }
}

void editor_font_t::discover(nk_context* context) {
    if (!context) return;
    const nk_command* command;
    nk_foreach(command, context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        observe({text->string, static_cast<std::size_t>(text->length)});
    }
}

void editor_font_t::refresh(nk_context* context, nk_context* overlay) {
    discover(context);
    discover(overlay);
    if (dirty_) rebuild(context);
    nk_style_set_font(context, &handle_);
    if (overlay) nk_style_set_font(overlay, &handle_);
}

void editor_font_t::rebuild(nk_context* context) {
    // Queued command fonts reference handle_, never the short-lived baked font.
    auto* atlas = nk_sdl_font_stash_begin(context);
    auto config = nk_font_config(handle_.height);
    config.range = latin_ranges;
    config.fallback_glyph = fallback_data_.empty() ? '?' : replacement;
    const auto latin_path = (directory_ / "AdwaitaMono-Regular.ttf").string();
    baked_ = nk_font_atlas_add_from_file(atlas, latin_path.c_str(), handle_.height, &config);
    if (!baked_) baked_ = nk_font_atlas_add_default(atlas, handle_.height, &config);
    ranges_.clear();
    for (const auto rune : codepoints_) {
        if (!ranges_.empty() && ranges_.back() + 1 == rune) {
            ranges_.back() = rune;
        } else {
            ranges_.push_back(rune);
            ranges_.push_back(rune);
        }
    }
    ranges_.push_back(0);
    if (!fallback_data_.empty()) {
        config.merge_mode = true;
        config.pixel_snap = true;
        config.oversample_h = config.oversample_v = 1;
        config.range = ranges_.data();
        nk_font_atlas_add_from_memory(atlas, fallback_data_.data(), fallback_data_.size(),
                                      handle_.height, &config);
    }
    nk_sdl_font_stash_end(context);
    glyphs_.clear();
    for (nk_rune index = 0; index < baked_->info.glyph_count; ++index)
        glyphs_.emplace(baked_->glyphs[index].codepoint, &baked_->glyphs[index]);
    handle_.texture = baked_->handle.texture;
    nk_style_set_font(context, &handle_);
    dirty_ = false;
}

auto editor_font_t::glyph(nk_rune rune) const -> const nk_font_glyph* {
    const auto found = glyphs_.find(rune);
    return found != glyphs_.end() ? found->second : (baked_ ? baked_->fallback : nullptr);
}

auto editor_font_t::width(nk_handle self, float height, const char* text, int length) -> float {
    auto& font = *static_cast<editor_font_t*>(self.ptr);
    float result{};
    while (length > 0) {
        nk_rune rune{};
        const auto consumed = nk_utf_decode(text, &rune, length);
        if (consumed <= 0) break;
        const auto* glyph = font.glyph(rune);
        // Newly encountered full-width text has sensible first-frame layout before refresh.
        result += !is_latin(rune) && !font.codepoints_.contains(rune) && font.supported(rune)
            ? font.glyph(replacement)->xadvance * height / font.handle_.height
            : glyph->xadvance * height / font.handle_.height;
        text += consumed;
        length -= consumed;
    }
    return result;
}

void editor_font_t::query(nk_handle self, float height, nk_user_font_glyph* glyph,
                          nk_rune codepoint, nk_rune next) {
    const auto& font = *static_cast<editor_font_t*>(self.ptr);
    (void)next;
    const auto* baked = font.glyph(codepoint);
    const auto scale = height / font.handle_.height;
    glyph->width = (baked->x1 - baked->x0) * scale;
    glyph->height = (baked->y1 - baked->y0) * scale;
    glyph->offset = nk_vec2(baked->x0 * scale, baked->y0 * scale);
    glyph->xadvance = baked->xadvance * scale;
    glyph->uv[0] = nk_vec2(baked->u0, baked->v0);
    glyph->uv[1] = nk_vec2(baked->u1, baked->v1);
}
}

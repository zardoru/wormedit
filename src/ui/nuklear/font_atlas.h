#pragma once

#include <wormedit/nuklear_config.h>
#include <filesystem>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace wormedit::ui::nuklear {
// Stable handle: queued text commands and the notification context survive atlas rebakes.
class editor_font_t {
  public:
    static constexpr std::size_t glyph_limit = 4096;
    void initialize(nk_context* context, const std::filesystem::path& directory);
    void refresh(nk_context* context, nk_context* overlay = nullptr);
    // Metrics remain in logical points; only atlas raster density changes.
    [[nodiscard]] auto set_raster_scale(float scale) -> bool;
    void reset();
    void observe(std::string_view text);
    [[nodiscard]] auto handle() const -> const nk_user_font* { return &handle_; }
    [[nodiscard]] auto display_handle() const -> const nk_user_font* { return &display_handle_; }
    [[nodiscard]] auto glyph(nk_rune codepoint) const -> const nk_font_glyph*;
    [[nodiscard]] auto glyph_count() const -> std::size_t { return codepoints_.size(); }
    [[nodiscard]] auto supported(nk_rune codepoint) const -> bool;
    [[nodiscard]] auto limited() const -> bool { return limited_; }
    // Bounds-checked OpenType format-12 cmap discovery, shared with asset regressions.
    static auto coverage(std::span<const unsigned char> bytes)
        -> std::vector<std::pair<nk_rune, nk_rune>>;

  private:
    static auto width(nk_handle self, float height, const char* text, int length) -> float;
    static void query(nk_handle self, float height, nk_user_font_glyph* glyph,
                      nk_rune codepoint, nk_rune next);
    void discover(nk_context* context);
    void rebuild(nk_context* context);
    void warn(const char* reason);
    nk_user_font handle_{};
    nk_user_font display_handle_{};
    nk_font* baked_{};
    std::filesystem::path directory_;
    std::vector<unsigned char> fallback_data_;
    std::vector<std::pair<nk_rune, nk_rune>> coverage_;
    std::set<nk_rune> codepoints_;
    std::vector<nk_rune> ranges_;
    std::unordered_map<nk_rune, const nk_font_glyph*> glyphs_;
    bool dirty_{};
    bool warned_{};
    bool limited_{};
    float raster_scale_{1}, requested_raster_scale_{1};
};
}

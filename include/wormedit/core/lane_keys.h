#pragma once
#include <wormedit/core/document.h>
#include <wormedit/core/preferences.h>

namespace wormedit {
// Rebuild only when the layout, authored fallback or personal override changes.
class lane_key_profile_cache_t {
  public:
    [[nodiscard]] auto resolve(const lane_key_preferences_t&, const lane_layout_t&,
                               const input_profile_t&) -> const input_profile_t&;
  private:
    lane_layout_t layout_;
    input_profile_t fallback_;
    lane_key_bindings_t keys_;
    std::optional<input_profile_t> resolved_;
};
}

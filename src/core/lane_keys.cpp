#include <wormedit/core/lane_keys.h>
#include <wormedit/core/lane_profiles.h>
#include "text_util.h"
#include <algorithm>

namespace wormedit {
namespace {
auto physical_key(std::uint16_t usage) -> physical_key_t {
    const auto found = std::ranges::find(physical_key_hid_usages, usage);
    return found == physical_key_hid_usages.end() ? physical_key_t::unknown
        : static_cast<physical_key_t>(found - physical_key_hid_usages.begin());
}
}
auto lane_key_bindings_t::is_valid() const -> bool {
    return keys.size() <= maximum_lane_key_bindings && std::ranges::all_of(keys, [](auto usage) {
        return usage == 0 || physical_key(usage) != physical_key_t::unknown;
    });
}
auto lane_key_preferences_t::is_valid() const -> bool {
    return layouts.size() <= maximum_lane_key_layouts && std::ranges::all_of(layouts, [](const auto& item) {
        return !item.first.empty() && text_util::is_valid_bounded_text(item.first, maximum_preference_string_bytes)
            && item.second.is_valid();
    });
}
auto lane_key_bindings_t::from(const lane_layout_t& layout, const input_profile_t& profile)
    -> lane_key_bindings_t {
    lane_key_bindings_t result;
    for (const auto& lane : layout.lanes_in_stage_order()) {
        const auto binding = std::ranges::find(profile.bindings, lane.id, &lane_binding_t::lane);
        result.keys.push_back(binding == profile.bindings.end() ? 0 : physical_key_hid_usage(binding->key));
    }
    return result;
}
auto lane_key_bindings_t::preset(const lane_layout_t& layout, const input_profile_t& fallback,
                                bool interleaved) -> lane_key_bindings_t {
    if (interleaved && !has_interleaved_preset(layout)) return from(layout, fallback);
    for (const auto& descriptor : new_project_layouts()) {
        const auto profile = interleaved ? descriptor.make_profile() : descriptor.profile();
        if (profile.layout.profile_id == layout.profile_id && profile.layout.lanes.size() == layout.lanes.size())
            return from(profile.layout, profile.input);
    }
    return from(layout, fallback);
}
auto lane_key_bindings_t::has_interleaved_preset(const lane_layout_t& layout) -> bool {
    const auto* descriptor = find_new_project_layout(layout.profile_id);
    if (!layout.split_stage || !descriptor || descriptor->source_family() != format_family_t::bms) return false;
    return std::ranges::any_of(built_in_bms_lane_profiles(), [&](const auto& entry) {
        return entry.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key &&
            is_bms_lane_layout_compatible(layout, entry.profile.layout);
    });
}
auto lane_key_bindings_t::profile_for(const lane_layout_t& layout, const input_profile_t& fallback) const
    -> result_t<input_profile_t, std::string> {
    if (!is_valid() || keys.size() != layout.lanes.size())
        return std::unexpected("The saved lane keys do not match this layout.");
    auto result = fallback;
    result.bindings.clear();
    const auto lanes = layout.lanes_in_stage_order();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (keys[index] == 0) continue;
        std::optional<std::uint32_t> stage;
        if (result.requires_focused_stage) stage = lanes[index].stage == stage_group_t::right ? 1U : 0U;
        result.bindings.push_back({.key = physical_key(keys[index]), .lane = lanes[index].id,
                                  .focused_stage = stage});
    }
    const auto diagnostics = validate_input_profile(layout, result);
    if (!diagnostics.empty()) return std::unexpected(diagnostics.front().message);
    return result;
}
auto lane_key_profile_cache_t::resolve(const lane_key_preferences_t& preferences,
    const lane_layout_t& layout, const input_profile_t& fallback) -> const input_profile_t& {
    const auto found = preferences.layouts.find(layout.profile_id);
    if (found == preferences.layouts.end()) return fallback;
    if (!resolved_ || layout_ != layout || fallback_ != fallback || keys_ != found->second) {
        layout_ = layout;
        fallback_ = fallback;
        keys_ = found->second;
        auto profile = keys_.profile_for(layout, fallback);
        resolved_ = profile ? std::move(*profile) : fallback;
    }
    return *resolved_;
}
} // namespace wormedit

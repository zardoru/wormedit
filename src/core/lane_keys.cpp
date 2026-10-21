#include <wormedit/core/lane_keys.h>
#include <wormedit/core/lane_profiles.h>
#include "text_util.h"
#include <algorithm>
#include <set>

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

auto default_lane_keys_t::conflicting_key() const -> std::optional<std::uint16_t> {
    for (const auto& [count, entry] : bindings) {
        std::set<std::uint16_t> used;
        for (const auto key : entry.keys)
            if (key && !used.insert(key).second) return key;
    }
    return {};
}
auto default_lane_keys_t::is_valid() const -> bool {
    return bindings.size() <= maximum_default_lane_count && !conflicting_key() &&
        std::ranges::all_of(bindings, [](const auto& entry) {
            return entry.first > 0 && entry.first <= maximum_default_lane_count &&
                entry.second.keys.size() == entry.first && entry.second.is_valid();
        });
}
auto default_lane_keys_t::keys_for(std::uint32_t count) const -> const lane_key_bindings_t& {
    static const auto rows = [] {
        std::array<lane_key_bindings_t, maximum_default_lane_count + 1> result;
        for (std::uint32_t index = 1; index < result.size(); ++index) result[index] = lane_key_bindings_t::number_row(index);
        return result;
    }();
    const auto found = bindings.find(count);
    return found == bindings.end() ? rows[count <= maximum_default_lane_count ? count : 0] : found->second;
}
auto lane_key_bindings_t::dfjk() -> lane_key_bindings_t {
    return {{physical_key_hid_usage(physical_key_t::key_d), physical_key_hid_usage(physical_key_t::key_f),
             physical_key_hid_usage(physical_key_t::key_j), physical_key_hid_usage(physical_key_t::key_k)}};
}
auto default_lane_keys_t::profile_for(const lane_layout_t& layout, const input_profile_t& fallback) const
    -> input_profile_t {
    const auto found = bindings.find(static_cast<std::uint32_t>(layout.lanes.size()));
    if (found == bindings.end()) return fallback;
    auto result = found->second.profile_for(layout, fallback);
    if (!result) return fallback;
    result->profile_id = "program-default-lane-keys";
    return std::move(*result);
}
void default_lane_keys_t::apply_to(song_document_t& document) const {
    for (auto& chart : document.charts) {
        if (chart.lane_layouts.size() != 1) continue;
        chart.input_profile = profile_for(chart.lane_layouts.front().layout, chart.input_profile);
    }
}
} // namespace wormedit

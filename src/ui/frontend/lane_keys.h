#pragma once
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::ui {
enum class lane_key_preset_t { wormedit, ibmsc, custom };
enum class lane_keys_action_kind_t { none, open, select_lane, bind, preset, reset, cancel, apply };
struct lane_key_lane_view_t {
    lane_id_t id;
    std::string name;
    std::string short_name;
    std::string key;
    std::uint16_t usage{};
    bool right_stage{};
    lane_key_color_t color{};
    auto operator==(const lane_key_lane_view_t&) const -> bool = default;
};
struct lane_key_keyboard_view_t {
    std::uint16_t usage{};
    std::string key;
    std::string lanes;
    bool right_stage{};
    unsigned row{};
    auto operator==(const lane_key_keyboard_view_t&) const -> bool = default;
};
struct lane_keys_view_t {
    bool visible{};
    std::uint64_t generation{};
    std::string layout;
    std::vector<lane_key_lane_view_t> lanes;
    std::vector<lane_key_keyboard_view_t> keyboard;
    lane_id_t selected;
    bool capturing{};
    bool can_apply{};
    bool can_interleave{};
    lane_key_preset_t preset{lane_key_preset_t::custom};
    std::string diagnostic;
    [[nodiscard]] auto is_selected(lane_id_t id) const -> bool { return selected == id; }
    auto operator==(const lane_keys_view_t&) const -> bool = default;
};
struct lane_keys_action_t {
    lane_keys_action_kind_t kind{};
    std::uint64_t generation{};
    lane_id_t lane;
    std::uint16_t usage{};
    lane_key_preset_t preset{};
    auto operator==(const lane_keys_action_t&) const -> bool = default;
};
}

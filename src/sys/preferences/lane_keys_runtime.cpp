#include "sys/preferences/lane_keys_runtime.h"
#include <wormedit/core/lane_profile_switch.h>
#include <algorithm>
#include <array>

namespace wormedit::sys {
namespace {
auto key_text(std::uint16_t usage) -> std::string {
    return usage ? shortcut_chord_text({.key = static_cast<shortcut_key_t>(usage)}) : "—";
}
auto short_lane_name(lane_role_t role, unsigned& number) -> std::string {
    if (role == lane_role_t::scratch) return "scr";
    if (role == lane_role_t::foot_pedal) return "ped";
    if (role == lane_role_t::free_zone) return "free";
    return std::to_string(++number);
}
}
auto lane_keys_runtime_t::open(const song_document_t& document, chart_id_t chart_id,
    local_position_t cursor, const program_preferences_t& preferences) -> bool {
    const auto* chart = document.find_chart(chart_id);
    if (!can_open(chart)) return false;
    const auto* segment = active_lane_layout_segment(*chart, cursor);
    layout_ = segment ? segment->layout : chart->lane_layouts.front().layout;
    if (layout_.profile_id.empty()) return false;
    chart_ = chart_id;
    project_ = document.id;
    fallback_ = chart->input_profile;
    lanes_ = layout_.lanes_in_stage_order();
    const auto found = preferences.lane_keys.layouts.find(layout_.profile_id);
    draft_ = found == preferences.lane_keys.layouts.end()
        ? lane_key_bindings_t::from(layout_, fallback_) : found->second;
    if (draft_.keys.size() != lanes_.size()) draft_ = lane_key_bindings_t::from(layout_, fallback_);
    ++view_.generation;
    view_.visible = true;
    view_.layout = layout_.display_name;
    view_.selected = {};
    view_.capturing = false;
    const auto defaults = lane_key_bindings_t::preset(layout_, fallback_, false);
    const auto interleaved = lane_key_bindings_t::preset(layout_, fallback_, true);
    view_.preset = draft_ == defaults ? ui::lane_key_preset_t::wormedit
        : draft_ == interleaved ? ui::lane_key_preset_t::ibmsc : ui::lane_key_preset_t::custom;
    view_.can_interleave = lane_key_bindings_t::has_interleaved_preset(layout_);
    reset_ = false;
    status_.clear();
    rebuild();
    return true;
}
void lane_keys_runtime_t::refresh(const song_document_t& document) {
    if (!view_.visible) return;
    const auto* chart = document.find_chart(chart_);
    const auto same = chart && document.id == project_ && std::ranges::any_of(chart->lane_layouts,
        [this](const auto& segment) { return segment.layout == layout_; });
    if (!same) { view_.visible = false; view_.capturing = false; }
}
void lane_keys_runtime_t::rebuild() {
    view_.lanes.clear();
    std::array<unsigned, 2> numbers{};
    for (std::size_t index = 0; index < lanes_.size(); ++index) {
        const auto& lane = lanes_[index];
        const auto stage = lane.stage == stage_group_t::right ? 1U : 0U;
        view_.lanes.push_back({.id = lane.id, .name = lane.name,
            .short_name = short_lane_name(lane.role, numbers[stage]), .key = key_text(draft_.keys[index]),
            .usage = draft_.keys[index], .right_stage = lane.stage == stage_group_t::right,
            .color = lane.key_color});
    }
    view_.keyboard.clear();
    for (std::size_t index = 1; index < physical_key_hid_usages.size(); ++index) {
        ui::lane_key_keyboard_view_t key{.usage = physical_key_hid_usages[index],
            .key = key_text(physical_key_hid_usages[index]),
            .row = index <= 10 ? 0U : index <= 20 ? 1U : index <= 29 ? 2U : 3U};
        for (const auto& lane : view_.lanes) {
            if (lane.usage != key.usage) continue;
            if (!key.lanes.empty()) key.lanes += ", ";
            key.lanes += (lane.right_stage ? "P2 " : "P1 ") + lane.short_name;
            key.right_stage = lane.right_stage;
        }
        view_.keyboard.push_back(std::move(key));
    }
    const auto candidate = draft_.profile_for(layout_, fallback_);
    view_.can_apply = candidate.has_value();
    view_.diagnostic = candidate ? "No duplicate keys" : candidate.error();
}
void lane_keys_runtime_t::bind(std::uint16_t usage, bool unbind) {
    const auto lane = std::ranges::find(lanes_, view_.selected, &lane_definition_t::id);
    if (lane == lanes_.end()) return;
    const lane_key_bindings_t candidate{.keys = {usage}};
    if (!unbind && (usage == 0 || !candidate.is_valid())) {
        view_.can_apply = false;
        view_.diagnostic = "Choose a letter or digit. Space, Tab, arrows and editing keys cannot be lane keys.";
        return;
    }
    draft_.keys[static_cast<std::size_t>(lane - lanes_.begin())] = unbind ? 0 : usage;
    reset_ = false;
    view_.capturing = false;
    view_.preset = ui::lane_key_preset_t::custom;
    rebuild();
}
void lane_keys_runtime_t::capture(std::span<const wormedit::ui::shortcut_event_t> events) {
    if (!view_.visible || !view_.capturing) return;
    const auto captured = capture_shortcut_batch(events);
    if (captured.cancel) { view_.capturing = false; rebuild(); return; }
    if (!captured.chord) return;
    const auto usage = static_cast<std::uint16_t>(captured.chord->key);
    bind(usage, captured.chord->key == shortcut_key_t::backspace);
}
auto lane_keys_runtime_t::apply(program_preferences_runtime_t& program) -> bool {
    if (!draft_.profile_for(layout_, fallback_)) return false;
    auto candidate = program.preferences;
    if (reset_) candidate.lane_keys.layouts.erase(layout_.profile_id);
    else candidate.lane_keys.layouts.insert_or_assign(layout_.profile_id, draft_);
    if (!serialize_program_preferences(candidate)) {
        view_.diagnostic = "Lane-key preferences exceed their storage limits.";
        return false;
    }
    status_ = apply_program_preferences(program, std::move(candidate));
    view_.visible = false;
    view_.capturing = false;
    return true;
}
auto lane_keys_runtime_t::dispatch(const ui::lane_keys_action_t& action,
    program_preferences_runtime_t& program) -> bool {
    using kind = ui::lane_keys_action_kind_t;
    if (!view_.visible || action.generation != view_.generation) return false;
    switch (action.kind) {
    case kind::select_lane:
        if (std::ranges::find(lanes_, action.lane, &lane_definition_t::id) == lanes_.end()) break;
        view_.selected = action.lane;
        view_.capturing = true;
        break;
    case kind::bind: bind(action.usage); break;
    case kind::preset:
        if (action.preset == ui::lane_key_preset_t::ibmsc && !view_.can_interleave) break;
        view_.preset = action.preset;
        if (action.preset == ui::lane_key_preset_t::custom) break;
        draft_ = lane_key_bindings_t::preset(layout_, fallback_, action.preset == ui::lane_key_preset_t::ibmsc);
        reset_ = false;
        view_.capturing = false;
        rebuild();
        break;
    case kind::reset:
        draft_ = lane_key_bindings_t::from(layout_, fallback_);
        reset_ = true;
        view_.capturing = false;
        rebuild();
        break;
    case kind::cancel: view_.visible = false; view_.capturing = false; break;
    case kind::apply: return apply(program);
    case kind::none:
    case kind::open: break;
    }
    return false;
}
}

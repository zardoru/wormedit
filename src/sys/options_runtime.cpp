#include "core/text_util.h"
#include "sys/options_runtime.h"
#include <wormedit/audio/playback_rate.h>
#include <algorithm>
#include <iterator>
#include <limits>

namespace wormedit::sys {
namespace {
void set_options_status(options_runtime_t& runtime, notification_severity_t severity,
                        std::string message) {
    runtime.status = std::move(message);
    runtime.notification = notification_post_t{
        .severity = severity, .source = notification_source_t::system,
        .message = runtime.status};
}
auto valid_category(WormUiOptionsCategory value) -> bool {
    return value >= WORM_UI_OPTIONS_GENERAL && value < WORM_UI_OPTIONS_CATEGORY_COUNT;
}
void copy_category(program_preferences_t& target, const program_preferences_t& source,
    WormUiOptionsCategory category) {
    switch (category) {
    case WORM_UI_OPTIONS_GENERAL:
        target.general.autosave_minutes = source.general.autosave_minutes;
        target.general.recent_files_count = source.general.recent_files_count;
        target.general.confirm_unsaved_close = source.general.confirm_unsaved_close;
        break;
    case WORM_UI_OPTIONS_EDITOR: target.editor = source.editor; target.minimap = source.minimap; break;
    case WORM_UI_OPTIONS_AUDIO: target.audio = source.audio; break;
    case WORM_UI_OPTIONS_DISPLAY: target.display = source.display; target.layout = source.layout; break;
    case WORM_UI_OPTIONS_INPUT: target.general.shortcut_profile_path = source.general.shortcut_profile_path; break;
    case WORM_UI_OPTIONS_PLAYERS: target.players = source.players; break;
    default: break;
    }
}
auto category_equal(const program_preferences_t& a, const program_preferences_t& b,
    WormUiOptionsCategory category) -> bool {
    auto copy = a;
    copy_category(copy, b, category);
    return copy == a;
}
void diagnose(options_runtime_t& runtime, WormUiOptionsField field, bool valid, const char* message) {
    if (!valid) runtime.diagnostics.push_back({field, message});
}
void validate_general(options_runtime_t& r) {
    diagnose(r, WORM_UI_OPTIONS_FIELD_AUTOSAVE, r.draft.general.autosave_minutes >= 1 &&
        r.draft.general.autosave_minutes <= 30, "Autosave must be between 1 and 30 minutes.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_RECENT_FILES, r.draft.general.recent_files_count >= 1 &&
        r.draft.general.recent_files_count <= 100, "Recent files must be between 1 and 100.");
}
void validate_editor(options_runtime_t& r) {
    const auto& e = r.draft.editor;
    diagnose(r, WORM_UI_OPTIONS_FIELD_SNAP, e.default_snap >= 1 && e.default_snap <= 1920,
        "Snap must be between 1 and 1920.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_ANCHOR, e.playhead_anchor_percent <= 100,
        "Following anchor must be between 0 and 100 percent.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, static_cast<std::uint32_t>(e.move_collision_policy) <= 1,
        "Choose Reject or Move it to a BGM lane.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_AREA_ALPHA, e.area_band_alpha <= 255,
        "Area-band opacity must be between 0 and 255.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION,
        std::ranges::all_of(e.scroll_directions, [](auto direction) { return direction <= 2; }),
        "Scroll direction must be Profile default, Up, or Down.");
}
void validate_display(options_runtime_t& r) {
    diagnose(r, WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME, is_valid_field_scheme_overrides(r.draft.display),
        "Layout skin overrides must contain at most 128 nonempty layout and skin names of at most 4096 bytes.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION, r.draft.display.scratch_positions[0] <= 3, "Choose Skin default, Chart layout, Left, or Right for Single / P1 scratch.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION, r.draft.display.scratch_positions[1] <= 3, "Choose Skin default, Chart layout, Left, or Right for P2 scratch.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY, r.draft.display.field_waveform_opacity_percent <= 100, "Field waveform opacity must be between 0 and 100 percent.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY, r.draft.display.snap_grid_opacity_percent <= 100, "Grid opacity must be between 0 and 100 percent.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME, text_util::is_valid_bounded_text(r.draft.display.field_scheme, maximum_preference_string_bytes), "Field scheme name is too long or contains a null character.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_THEME, text_util::is_valid_bounded_text(r.draft.display.theme, maximum_preference_string_bytes), "Theme path is too long or contains a null character.");
    const auto scale = r.draft.display.ui_scale_percent;
    diagnose(r, WORM_UI_OPTIONS_FIELD_SCALE, scale == 0 || (scale >= 100 && scale <= 200), "Scale must be automatic or between 100 and 200 percent.");
    diagnose(r, WORM_UI_OPTIONS_FIELD_LAYOUT, text_util::is_valid_bounded_text(r.draft.layout.dock_layout, maximum_preference_string_bytes), "Layout is too long or contains a null character.");
}
void validate_draft(options_runtime_t& r) {
    r.diagnostics.clear();
    switch (r.category) {
    case WORM_UI_OPTIONS_GENERAL: validate_general(r); break;
    case WORM_UI_OPTIONS_EDITOR: validate_editor(r); break;
    case WORM_UI_OPTIONS_AUDIO:
        diagnose(r, WORM_UI_OPTIONS_FIELD_OSU_SKIN_DIRECTORY,
            text_util::is_valid_bounded_text(r.draft.audio.osu_skin_directory, maximum_preference_string_bytes),
            "The osu! skin directory must be bounded text without NUL characters.");
        diagnose(r, WORM_UI_OPTIONS_FIELD_RATE,
            std::ranges::find(audio::playback_rates, r.draft.audio.playback_rate) != audio::playback_rates.end(),
            "Playback rate is unsupported."); break;
    case WORM_UI_OPTIONS_DISPLAY: validate_display(r); break;
    case WORM_UI_OPTIONS_INPUT:
        diagnose(r, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH, text_util::is_valid_bounded_text(r.draft.general.shortcut_profile_path, maximum_preference_string_bytes) &&
            !r.draft.general.shortcut_profile_path.empty(), "Shortcut profile path must be nonempty and at most 4096 bytes."); break;
    case WORM_UI_OPTIONS_PLAYERS:
        diagnose(r, WORM_UI_OPTIONS_FIELD_PLAYERS, is_valid_external_player_preferences(r.draft.players), "External-player definitions are invalid."); break;
    default: break;
    }
    auto candidate = program_preferences_t::with_defaults();
    copy_category(candidate, r.draft, r.category);
    if (r.diagnostics.empty() && !serialize_program_preferences(candidate))
        r.diagnostics.push_back({WORM_UI_OPTIONS_FIELD_PAGE, "This page contains invalid text or oversized values."});
}
auto apply_category(options_runtime_t& r, program_preferences_runtime_t& program) -> bool {
    validate_draft(r);
    if (!r.diagnostics.empty()) { set_options_status(r, notification_severity_t::warning, "Correct this page's invalid fields before applying."); return false; }
    if (!category_equal(program.preferences, r.baseline, r.category)) {
        set_options_status(r, notification_severity_t::warning, "These settings changed outside Options. Revert this page before applying.");
        return false;
    }
    if (category_equal(program.preferences, r.draft, r.category)) { set_options_status(r, notification_severity_t::info, "This page has no changes."); return false; }
    auto candidate = program.preferences;
    copy_category(candidate, r.draft, r.category);
    if (!serialize_program_preferences(candidate)) { set_options_status(r, notification_severity_t::warning, "Program preferences contain invalid or oversized values."); return false; }
    const auto error = apply_program_preferences(program, std::move(candidate));
    copy_category(r.baseline, program.preferences, r.category);
    set_options_status(r, error.empty() ? notification_severity_t::success : notification_severity_t::error,
                       error.empty() ? "Options saved." : error);
    return true;
}
auto edit_general(program_preferences_t& p, const WormUiOptionsAction& a) -> bool {
    switch (a.field) {
    case WORM_UI_OPTIONS_FIELD_AUTOSAVE: p.general.autosave_minutes = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_RECENT_FILES: p.general.recent_files_count = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_CONFIRM_CLOSE: p.general.confirm_unsaved_close = a.boolean; return true;
    default: return false;
    }
}
auto edit_editor(program_preferences_t& p, const WormUiOptionsAction& a) -> bool {
    switch (a.field) {
    case WORM_UI_OPTIONS_FIELD_SNAP: p.editor.default_snap = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION:
        if (a.family_index >= p.editor.scroll_directions.size()) return false;
        p.editor.scroll_directions[a.family_index] = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_ANCHOR: p.editor.playhead_anchor_percent = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY: p.editor.move_collision_policy = static_cast<move_collision_policy_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_AREA_ALPHA: p.editor.area_band_alpha = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_NOTE_PALETTE: p.editor.note_kind_palette_visible = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_LANE_LEGEND: p.editor.lane_key_legend_visible = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK: p.editor.audition_on_click = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_INVERT_SCROLL_WHEEL: p.editor.invert_scroll_wheel = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_MINIMAP_CLICK: p.minimap.click_moves_paused_playhead = a.boolean; return true;
    default: return false;
    }
}
auto edit_audio(program_preferences_t& p, const WormUiOptionsAction& a) -> bool {
    switch (a.field) {
    case WORM_UI_OPTIONS_FIELD_RATE: p.audio.playback_rate = {a.integer, a.denominator}; return true;
    case WORM_UI_OPTIONS_FIELD_KEEP_PITCH: p.audio.keep_pitch = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_OSU_SKIN_DIRECTORY: p.audio.osu_skin_directory = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY:
        p.audio.keysound_budget_mib = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(a.integer, 256, 8192));
        return true;
    default: return false;
    }
}
auto edit_layout_field_scheme(program_preferences_t& p, const WormUiOptionsAction& action,
                              std::string_view layout_key) -> bool {
    if (layout_key.empty() || !text_util::is_valid_bounded_text(layout_key, maximum_preference_string_bytes)) return false;
    auto& overrides = p.display.field_scheme_overrides;
    if (!action.text[0]) overrides.erase(std::string{layout_key});
    else overrides.insert_or_assign(std::string{layout_key}, action.text);
    return is_valid_field_scheme_overrides(p.display);
}

auto edit_display(program_preferences_t& p, const WormUiOptionsAction& a,
                  std::string_view layout_key) -> bool {
    switch (a.field) {
    case WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION: p.display.scratch_positions[0] = static_cast<std::uint32_t>(std::min<std::uint64_t>(4, a.integer)); return true;
    case WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION: p.display.scratch_positions[1] = static_cast<std::uint32_t>(std::min<std::uint64_t>(4, a.integer)); return true;
    case WORM_UI_OPTIONS_FIELD_FIELD_SCHEME: p.display.field_scheme = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME: return edit_layout_field_scheme(p, a, layout_key);
    case WORM_UI_OPTIONS_FIELD_SNAP_COLORED_GRID: p.display.snap_colored_grid = a.boolean; return true;
    case WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY:
        p.display.snap_grid_opacity_percent = static_cast<std::uint32_t>(std::min<std::uint64_t>(100, a.integer)); return true;
    case WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY:
        p.display.field_waveform_opacity_percent = static_cast<std::uint32_t>(std::min<std::uint64_t>(100, a.integer)); return true;
    case WORM_UI_OPTIONS_FIELD_THEME: p.display.theme = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_SCALE: p.display.ui_scale_percent = static_cast<std::uint32_t>(a.integer); return true;
    case WORM_UI_OPTIONS_FIELD_LAYOUT: p.layout.dock_layout = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_TIMING_OVERLAY: p.display.frame_timing_overlay = a.boolean; return true;
    default: return false;
    }
}
auto edit_player_text(external_player_definition_t& p, const WormUiOptionsAction& a) -> bool {
    switch (a.field) {
    case WORM_UI_OPTIONS_FIELD_PLAYER_NAME: p.name = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_PLAYER_PATH: p.path = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_PLAYER_START: p.start_arguments = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_PLAYER_HERE: p.here_arguments = a.text; return true;
    case WORM_UI_OPTIONS_FIELD_PLAYER_STOP: p.stop_arguments = a.text; return true;
    default: return false;
    }
}
auto edit_players(program_preferences_t& p, const WormUiOptionsAction& a) -> bool {
    auto& players = p.players;
    if (a.field == WORM_UI_OPTIONS_FIELD_PLAYER_ADD) {
        if (players.players.size() >= maximum_external_players) return false;
        players.players.push_back({});
        return true;
    }
    if (a.player_index >= players.players.size()) return false;
    if (a.field == WORM_UI_OPTIONS_FIELD_PLAYER_SELECTED) { players.selected = a.player_index; return true; }
    if (a.field == WORM_UI_OPTIONS_FIELD_PLAYER_REMOVE) {
        if (players.players.size() == 1) return false;
        players.players.erase(players.players.begin() + a.player_index);
        if (players.selected > a.player_index) --players.selected;
        players.selected = std::min(players.selected, players.players.size() - 1);
        return true;
    }
    return edit_player_text(players.players[a.player_index], a);
}
auto bounded_edit(const WormUiOptionsAction& action) -> bool {
    if (std::ranges::find(action.text, '\0') == std::end(action.text)) return false;
    const bool small_integer = action.field == WORM_UI_OPTIONS_FIELD_AUTOSAVE ||
        action.field == WORM_UI_OPTIONS_FIELD_RECENT_FILES || action.field == WORM_UI_OPTIONS_FIELD_SNAP ||
        action.field == WORM_UI_OPTIONS_FIELD_SCALE || action.field == WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION ||
        action.field == WORM_UI_OPTIONS_FIELD_ANCHOR || action.field == WORM_UI_OPTIONS_FIELD_AREA_ALPHA;
    return !small_integer || action.integer <= std::numeric_limits<std::uint32_t>::max();
}
auto edit_field(options_runtime_t& r, const WormUiOptionsAction& action) -> bool {
    if (action.category != r.category || !bounded_edit(action)) return false;
    auto candidate = r.draft;
    bool edited{};
    switch (r.category) {
    case WORM_UI_OPTIONS_GENERAL: edited = edit_general(candidate, action); break;
    case WORM_UI_OPTIONS_EDITOR: edited = edit_editor(candidate, action); break;
    case WORM_UI_OPTIONS_AUDIO: edited = edit_audio(candidate, action); break;
    case WORM_UI_OPTIONS_DISPLAY: edited = edit_display(candidate, action, r.current_field_layout_key); break;
    case WORM_UI_OPTIONS_INPUT:
        if (action.field != WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH) return false;
        candidate.general.shortcut_profile_path = action.text; edited = true; break;
    case WORM_UI_OPTIONS_PLAYERS: edited = edit_players(candidate, action); break;
    default: return false;
    }
    return edited && edit_options_draft(r, action.revision, candidate);
}
} // namespace

auto toggle_audition_on_click(program_preferences_runtime_t& program, bool& enabled) -> std::string {
    auto candidate = program.preferences;
    candidate.editor.audition_on_click = !enabled;
    auto error = apply_program_preferences(program, std::move(candidate));
    enabled = program.preferences.editor.audition_on_click;
    return error;
}
void set_options_field_layout(options_runtime_t& r, std::string_view key, std::string_view name) {
    if (!text_util::is_valid_bounded_text(key, maximum_preference_string_bytes)) key = {};
    if (!text_util::is_valid_bounded_text(name, maximum_preference_string_bytes)) name = {};
    if (key != r.current_field_layout_key && r.visible) ++r.revision;
    r.current_field_layout_key = key;
    r.current_field_layout_name = name;
}
void open_options(options_runtime_t& r, const program_preferences_t& preferences) {
    if (r.visible) return;
    r.visible = true;
    r.draft = preferences;
    r.baseline = preferences;
    r.status.clear();
    ++r.revision;
    validate_draft(r);
}
auto resolve_scratch_positions(const display_preferences_t& preferences,
    const std::array<std::optional<bool>, 2>& skin_defaults)
    -> std::array<std::optional<bool>, 2> {
    std::array<std::optional<bool>, 2> resolved{};
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        switch (preferences.scratch_positions[index]) {
        case 0: resolved[index] = skin_defaults[index]; break;
        case 2: resolved[index] = false; break;
        case 3: resolved[index] = true; break;
        default: break; // Chart layout or an invalid override preserves authored placement.
        }
    }
    return resolved;
}
auto edit_options_draft(options_runtime_t& r, std::uint64_t revision,
    const program_preferences_t& candidate) -> bool {
    if (!r.visible || revision != r.revision) return false;
    copy_category(r.draft, candidate, r.category);
    r.draft.audio.keysound_budget_mib = std::clamp(r.draft.audio.keysound_budget_mib, 256U, 8192U);
    ++r.revision;
    r.status.clear();
    validate_draft(r);
    return true;
}
auto options_view(options_runtime_t& r) -> WormUiOptionsView {
    // Validation serializes the draft; a hidden window shows no diagnostics.
    if (r.visible) validate_draft(r);
    r.diagnostic_views.clear();
    for (const auto& item : r.diagnostics) r.diagnostic_views.push_back({item.field, item.message.c_str()});
    r.player_views.clear();
    for (const auto& player : r.draft.players.players)
        r.player_views.push_back({player.name.c_str(), player.path.c_str(), player.start_arguments.c_str(),
            player.here_arguments.c_str(), player.stop_arguments.c_str()});
    const auto& p = r.draft;
    return {.visible = r.visible, .revision = r.revision, .category = r.category,
        .changed = !category_equal(p, r.baseline, r.category), .can_apply = r.diagnostics.empty(),
        .autosave_minutes = p.general.autosave_minutes, .recent_files_count = p.general.recent_files_count,
        .default_snap = p.editor.default_snap, .ui_scale_percent = p.display.ui_scale_percent,
        .scroll_directions = {p.editor.scroll_directions[0], p.editor.scroll_directions[1],
            p.editor.scroll_directions[2], p.editor.scroll_directions[3], p.editor.scroll_directions[4],
            p.editor.scroll_directions[5]},
        .playhead_anchor_percent = p.editor.playhead_anchor_percent, .area_band_alpha = p.editor.area_band_alpha,
        .move_collision_policy = static_cast<std::uint32_t>(p.editor.move_collision_policy),
        .note_kind_palette_visible = p.editor.note_kind_palette_visible, .lane_key_legend_visible = p.editor.lane_key_legend_visible,
        .audition_on_click = p.editor.audition_on_click,
        .invert_scroll_wheel = p.editor.invert_scroll_wheel,
        .confirm_unsaved_close = p.general.confirm_unsaved_close, .keep_pitch = p.audio.keep_pitch,
        .frame_timing_overlay = p.display.frame_timing_overlay,
        .snap_colored_grid = p.display.snap_colored_grid,
        .snap_grid_opacity_percent = p.display.snap_grid_opacity_percent,
        .field_waveform_opacity_percent = p.display.field_waveform_opacity_percent,
        .minimap_click_moves_paused_playhead = p.minimap.click_moves_paused_playhead,
        .keysound_budget_mib = p.audio.keysound_budget_mib,
        .osu_skin_directory = p.audio.osu_skin_directory,
        .rate_numerator = p.audio.playback_rate.numerator, .rate_denominator = p.audio.playback_rate.denominator,
        .p1_scratch_position = p.display.scratch_positions[0], .p2_scratch_position = p.display.scratch_positions[1],
        .field_scheme = p.display.field_scheme.c_str(),
        .current_field_layout_key = r.current_field_layout_key.c_str(),
        .current_field_layout_name = r.current_field_layout_name.c_str(),
        .current_layout_field_scheme = p.display.field_scheme_overrides.contains(r.current_field_layout_key)
            ? p.display.field_scheme_overrides.at(r.current_field_layout_key).c_str() : "",
        .theme = p.display.theme.c_str(), .dock_layout = p.layout.dock_layout.c_str(),
        .shortcut_profile_path = p.general.shortcut_profile_path.c_str(), .players = r.player_views.data(),
        .player_count = r.player_views.size(), .selected_player = p.players.selected,
        .diagnostics = r.diagnostic_views.data(), .diagnostic_count = r.diagnostic_views.size(), .status = r.status.c_str()};
}
auto apply_options_action(options_runtime_t& r, program_preferences_runtime_t& program,
    const WormUiOptionsAction& action) -> bool {
    r.notification.reset();
    if (action.kind == WORM_UI_OPTIONS_NONE) return false;
    if (action.kind == WORM_UI_OPTIONS_OPEN) { open_options(r, program.preferences); return false; }
    if (!r.visible || action.revision != r.revision) return false;
    if (action.kind == WORM_UI_OPTIONS_EDIT_FIELD) {
        (void)edit_field(r, action);
        return false; // Draft edits never accept preferences or publish a file.
    }
    bool accepted = false;
    switch (action.kind) {
    case WORM_UI_OPTIONS_CLOSE: r.visible = false; break;
    case WORM_UI_OPTIONS_SELECT_CATEGORY:
        if (!valid_category(action.category)) return false;
        r.category = action.category; r.status.clear(); break;
    case WORM_UI_OPTIONS_APPLY: accepted = apply_category(r, program); break;
    case WORM_UI_OPTIONS_REVERT:
        copy_category(r.draft, program.preferences, r.category);
        copy_category(r.baseline, program.preferences, r.category); r.status.clear(); break;
    case WORM_UI_OPTIONS_DEFAULTS: copy_category(r.draft, program_preferences_t::with_defaults(), r.category); r.status.clear(); break;
    default: return false;
    }
    ++r.revision;
    validate_draft(r);
    return accepted;
}
} // namespace wormedit::sys

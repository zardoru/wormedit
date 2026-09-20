#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/platform_recovery.h"
#include "sys/platform_shortcut_profile.h"
#include "ui/frontend.h"
#include "wormedit/config.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/startup_session.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/viewport_input.h>
#include <wormedit/core/workspace.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/sdl_audition_output.h"
#include "sys/sdl_preview_output.h"

#include <wormedit/audio/object_audition.h>
#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/waveform_job.h>
#endif

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::array<const char*, WORM_UI_WORKSPACE_TAB_COUNT> tab_labels{
    "Setup", "Metadata", "Timing", "Compose", "Storyboard", "Audio / Slices", "Compare / Validate",
};

auto new_session_project_id() -> wormedit::song_id_t {
    try {
        std::random_device source;
        const auto value =
            (static_cast<std::uint64_t>(source()) << 32U) | static_cast<std::uint64_t>(source());
        if (value != 0)
            return wormedit::song_id_t{value};
    } catch (...) {
    }
    const auto wall =
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    const auto monotonic =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto value = wall ^ (monotonic + 0x9e3779b97f4a7c15ULL + (wall << 6U) + (wall >> 2U));
    return wormedit::song_id_t{value == 0 ? 1 : value};
}

auto initial_document(wormedit::song_id_t project_id) -> wormedit::song_document_t {
    auto profile = wormedit::make_bms_7k_profile();
    wormedit::chart_document_t chart{
        .id = wormedit::chart_id_t{2},
        .metadata = {.name = "New Difficulty", .author = ""},
        .source_family = wormedit::format_family_t::native_project,
        .measures = {{.id = wormedit::measure_id_t{3}}},
        .lane_layouts = {{.id = wormedit::object_id_t{4},
                          .position = {wormedit::measure_id_t{3}, 0},
                          .layout = std::move(profile.layout)}},
        .input_profile = std::move(profile.input),
        .bpms = {{.id = wormedit::object_id_t{5},
                  .position = {wormedit::measure_id_t{3}, 0},
                  .beats_per_minute = {120, 1}}},
    };
    return {
        .id = project_id,
        .revision = 1,
        .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
        .charts = {std::move(chart)},
    };
}

struct flattened_workspace_t {
    WormUiWorkspaceView view{};
    std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_path_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_disabled_storage;
    std::array<std::string, WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY> recovery_diagnostic_storage;
    std::vector<const char*> metadata_search_tag_storage;
    std::vector<std::size_t> metadata_search_tag_length_storage;
    std::array<WormUiMetadataFormatFieldView, WORM_UI_METADATA_FORMAT_FIELD_CAPACITY>
        metadata_format_field_storage{};
    std::array<WormUiMetadataFormatTargetView, WORM_UI_METADATA_FORMAT_TARGET_CAPACITY>
        metadata_format_target_storage{};
    std::string timing_status_storage;
    std::string timing_recipe_status_storage;
    std::vector<WormUiTimingSpeedView> timing_speed_storage;
    std::vector<WormUiTimingMarkerView> timing_marker_storage;
    std::vector<WormUiTimingGridLineView> timing_grid_storage;
    std::vector<WormUiTimingRecipePoint> timing_recipe_candidate_storage;
    std::vector<WormUiTimingRecipePoint> timing_recipe_composed_storage;
    std::string compose_status_storage;
    std::string compose_hover_tooltip_storage;
    std::string compose_selected_note_label_storage;
    std::vector<WormUiComposeBandView> compose_band_storage;
    std::vector<WormUiComposeLaneView> compose_lane_storage;
    std::vector<WormUiComposeGridLineView> compose_grid_storage;
    std::vector<WormUiComposeDifferenceView> compose_difference_storage;
    std::vector<WormUiComposeHoldView> compose_hold_storage;
    std::vector<WormUiComposeNoteView> compose_note_storage;
    std::vector<WormUiComposeBgmLaneView> compose_bgm_lane_storage;
    std::vector<WormUiComposeBgmEventView> compose_bgm_event_storage;
    std::optional<WormUiComposeCursorView> compose_cursor_storage;
    std::array<WormUiComposeSemanticLayerView, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY>
        compose_semantic_layer_storage{};
    std::array<std::string, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY>
        compose_semantic_layer_name_storage;
    std::array<WormUiComposeBranchRowView, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY>
        compose_branch_row_storage{};
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY> compose_branch_text_storage;
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY>
        compose_branch_diagnostic_storage;
    std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
        compose_lane_key_storage{};
    std::optional<wormedit::editor_interaction_view_model_t> compose_interaction_view_storage;
    std::vector<std::uint64_t> audio_asset_storage;
    std::vector<WormUiAudioWaveformColumnView> audio_waveform_storage;
};

struct compose_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    std::optional<wormedit::editor_field_scene_index_t> index;
    std::optional<wormedit::editor_hit_test_index_t> hit_test_index;
    std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
    std::optional<wormedit::editor_object_hover_state_t> hover;
    std::optional<wormedit::editor_object_audition_request_t> audition_request;
    std::unordered_map<std::uint64_t, std::size_t> semantic_layer_membership_counts;
    bool focus_note_label_editor{};
    bool viewport_autoscroll_active{};
    double viewport_autoscroll_anchor_x_pixels{};
    double viewport_autoscroll_anchor_y_pixels{};
    double viewport_autoscroll_pointer_x_pixels{};
    double viewport_autoscroll_pointer_y_pixels{};
    std::string status;
};

struct application_command_state_t {
    bool timing_locked{};
    bool note_paint_mode_active{};
    bool semantic_layer_paint_mode_active{};
    bool semantic_layer_solo_active{};
    std::optional<wormedit::layer_id_t> semantic_layer_target;
    // Semantic-layer filters are an ephemeral per-chart Compose view setting.
    // Layer IDs are only unique within their chart, so never carry a filter to
    // another selected difficulty.
    std::optional<wormedit::chart_id_t> semantic_layer_filter_chart;
    std::vector<wormedit::layer_id_t> required_semantic_layer_filters;
    std::vector<wormedit::layer_id_t> excluded_semantic_layer_filters;
    std::optional<wormedit::chart_id_t> semantic_layer_palette_chart;
    std::optional<wormedit::layer_id_t> semantic_layer_palette_target;
    std::size_t semantic_layer_palette_page{};
    // UI-only, chart-scoped preferences. Changing a snap never changes chart
    // data; an active gesture keeps its captured snap until it is resolved.
    std::unordered_map<std::uint64_t, std::uint64_t> requested_snap_denominators;
    std::unordered_map<std::uint64_t, double> bgm_horizontal_scroll_pixels;
    std::function<bool()> transport_active;
    std::function<std::string()> transport_play_pause;
    std::function<std::string(std::uint64_t)> transport_seek;
    std::function<void()> transport_stop;
    std::optional<double> preview_playhead_absolute_row;
};

struct timing_grid_state_t {
    wormedit::chart_id_t chart;
    std::vector<std::uint64_t> supported_denominators;
    std::uint64_t active_denominator{};
    bool active_was_fallback{};
};

struct timing_speed_cache_entry_t {
    std::uint64_t object_id{};
    std::uint64_t source_order{};
    std::uint64_t storage_order{};
    long double marker_row{};
    long double affected_begin_row{};
    long double affected_end_row{};
    double multiplier{};
    double transition_duration{};
    bool transition_is_seconds{};
    bool transition_clipped{};
};

struct timing_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    bool truncated{};
    std::uint64_t total_rows{};
    std::vector<timing_speed_cache_entry_t> speeds;
    std::string status;
};

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool;

auto selected_chart(const wormedit::document_history_t& history,
                    const wormedit::workspace_controller_t& workspace)
    -> const wormedit::chart_document_t*;

auto visible_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t*;

auto recovery_status_name(wormedit::native_project_recovery_candidate_status_t status)
    -> std::string_view {
    using status_t = wormedit::native_project_recovery_candidate_status_t;
    switch (status) {
    case status_t::missing:
        return "missing";
    case status_t::valid:
        return "valid";
    case status_t::invalid:
        return "invalid";
    case status_t::unreadable:
        return "unreadable";
    case status_t::resource_limited:
        return "resource limited";
    }
    return "unknown";
}

auto recovery_comparison_name(wormedit::native_project_recovery_comparison_t comparison)
    -> std::string_view {
    using comparison_t = wormedit::native_project_recovery_comparison_t;
    switch (comparison) {
    case comparison_t::not_comparable:
        return "not compared";
    case comparison_t::no_valid_primary:
        return "no valid primary";
    case comparison_t::different_project:
        return "different project";
    case comparison_t::older_revision:
        return "older";
    case comparison_t::equal_revision:
        return "same revision";
    case comparison_t::newer_revision:
        return "newer";
    }
    return "unknown comparison";
}

auto format_family_name(wormedit::format_family_t family) -> std::string_view {
    using family_t = wormedit::format_family_t;
    switch (family) {
    case family_t::native_project:
        return "Native";
    case family_t::bms:
        return "BMS";
    case family_t::stepmania:
        return "StepMania";
    case family_t::osu_mania:
        return "osu!mania";
    case family_t::bmson:
        return "BMSON";
    case family_t::midi:
        return "MIDI";
    case family_t::quaver:
        return "Quaver";
    }
    return "Unknown";
}

auto metadata_value_summary(std::string_view value) -> std::string {
    constexpr std::size_t readable_value_limit = 96;
    if (value.size() <= readable_value_limit)
        return std::string(value);
    return "<" + std::to_string(value.size()) + " UTF-8 bytes>";
}

auto recovery_action_mask(const std::vector<wormedit::native_project_recovery_action_t>& actions)
    -> size_t {
    using action_t = wormedit::native_project_recovery_action_t;
    size_t mask{};
    for (const auto action : actions) {
        switch (action) {
        case action_t::inspect_read_only:
            mask |= WORM_UI_RECOVERY_ACTION_INSPECT;
            break;
        case action_t::recover_into_session:
            mask |= WORM_UI_RECOVERY_ACTION_RECOVER;
            break;
        case action_t::save_as_new_project:
            mask |= WORM_UI_RECOVERY_ACTION_SAVE_AS;
            break;
        case action_t::discard:
            mask |= WORM_UI_RECOVERY_ACTION_DISCARD;
            break;
        case action_t::quarantine:
            mask |= WORM_UI_RECOVERY_ACTION_QUARANTINE;
            break;
        }
    }
    return mask;
}

auto flatten_workspace(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured) -> flattened_workspace_t {
    flattened_workspace_t result;
    result.view.project_title = document.metadata.title.c_str();
    result.view.artist = document.metadata.artist.c_str();
    result.view.selected_difficulty = source.selected_difficulty.has_value()
                                          ? source.selected_difficulty->difficulty_name.c_str()
                                          : "No difficulty";
    result.view.status_text = status.c_str();
    result.view.git_hash = WORMEDIT_GIT_HASH;
    result.view.active_tab = static_cast<WormUiWorkspaceTab>(source.active_tab);
    result.view.history_available = source.history.available;
    result.view.dirty = source.history.dirty;
    result.view.undo_depth = source.history.undo_depth;
    result.view.redo_depth = source.history.redo_depth;
    result.view.recovery_panel_visible = true;
    result.view.recovery_heading = "Project recovery";
    result.view.recovery_rows_truncated = recovery.rows_truncated;
    if (recovery_scan_configured) {
        result.view.recovery_notice = recovery.banner_visible
                                          ? "Review candidates before choosing an action."
                                          : "No recovery candidates currently require action.";
    } else {
        result.view.recovery_notice =
            "Unavailable: this starter workspace has no opened project path or configured "
            "recovery directory.";
    }
    const auto recovery_row_count =
        std::min(recovery.rows.size(), static_cast<std::size_t>(WORM_UI_RECOVERY_ROW_CAPACITY));
    result.view.recovery_candidate_count = recovery_row_count;
    result.view.recovery_rows_truncated =
        result.view.recovery_rows_truncated || recovery_row_count < recovery.rows.size();
    for (std::size_t index = 0; index < recovery_row_count; ++index) {
        const auto& row = recovery.rows[index];
        result.recovery_path_storage[index] = row.path_label;
        auto& summary = result.recovery_summary_storage[index];
        summary = std::string(recovery_status_name(row.status)) + " · " +
                  std::string(recovery_comparison_name(row.comparison));
        if (row.version) {
            summary += " · revision " + std::to_string(row.version->revision);
        }
        if (row.recovery_session_sequence) {
            summary += " · session " + std::to_string(*row.recovery_session_sequence);
        }
        if (recovery.preferred_recovery_row == index) {
            summary += " · recommended recovery";
        }
        auto& disabled_reason = result.recovery_disabled_storage[index];
        const auto action_mask = recovery_action_mask(row.available_actions);
        constexpr auto all_recovery_actions =
            WORM_UI_RECOVERY_ACTION_INSPECT | WORM_UI_RECOVERY_ACTION_RECOVER |
            WORM_UI_RECOVERY_ACTION_SAVE_AS | WORM_UI_RECOVERY_ACTION_DISCARD |
            WORM_UI_RECOVERY_ACTION_QUARANTINE;
        if (row.available_actions.empty()) {
            disabled_reason = "No safe action is available for this candidate.";
        } else if (action_mask != all_recovery_actions) {
            disabled_reason =
                "Unavailable actions are disabled by the recovery controller's safety checks.";
        }
        result.view.recovery_candidates[index] = {
            .path_label = result.recovery_path_storage[index].c_str(),
            .summary = summary.c_str(),
            .disabled_reason = disabled_reason.empty() ? nullptr : disabled_reason.c_str(),
            .action_mask = action_mask,
            .scan_generation = row.target.scan_generation,
            .row_index = row.target.row_index,
            .candidate_index = row.target.candidate_index,
        };
    }
    const auto recovery_diagnostic_count =
        std::min(recovery.diagnostics.size(),
                 static_cast<std::size_t>(WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY));
    result.view.recovery_diagnostic_count = recovery_diagnostic_count;
    for (std::size_t index = 0; index < recovery_diagnostic_count; ++index) {
        auto& diagnostic = result.recovery_diagnostic_storage[index];
        diagnostic = recovery.diagnostics[index].message;
        if (!recovery.diagnostics[index].path.empty()) {
            diagnostic += " (" + recovery.diagnostics[index].path.generic_string() + ")";
        }
        result.view.recovery_diagnostics[index] = diagnostic.c_str();
    }
    for (std::size_t tab = 0; tab < tab_labels.size(); ++tab) {
        result.view.tabs[tab] = {
            .label = tab_labels[tab],
            .enabled = wormedit::workspace_tab_enabled(static_cast<wormedit::workspace_tab_t>(tab)),
        };
    }
    result.view.tab_heading = tab_labels[static_cast<std::size_t>(source.active_tab)];

    const auto append = [&result](std::string line) {
        if (result.view.summary_line_count >= result.summary_storage.size()) {
            return;
        }
        const auto index = result.view.summary_line_count++;
        result.summary_storage[index] = std::move(line);
    };
    std::visit(
        [&](const auto& tab) {
            using tab_t = std::remove_cvref_t<decltype(tab)>;
            if constexpr (std::is_same_v<tab_t, wormedit::workspace_setup_view_t>) {
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                append("Charts: " + std::to_string(tab.charts.size()));
                append("Audio assets: " + std::to_string(tab.audio_asset_count));
                append("Visual assets: " + std::to_string(tab.visual_asset_count));
                append("Use the tabs above to edit one shared song workspace.");
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_metadata_view_t>) {
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                auto& metadata = result.view.metadata;
                metadata.visible = true;
                metadata.document_revision = source.document_revision;
                metadata.title = tab.title.c_str();
                metadata.artist = tab.artist.c_str();
                metadata.title_bytes = tab.title.size();
                metadata.artist_bytes = tab.artist.size();
                metadata.search_tag_count = tab.search_tags.size();
                metadata.song_fields_editable =
                    tab.title.size() <= WORM_UI_METADATA_TITLE_BYTES &&
                    tab.artist.size() <= WORM_UI_METADATA_TITLE_BYTES &&
                    tab.search_tags.size() <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY;
                metadata.search_tags_editable =
                    tab.search_tags.size() <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY;
                if (metadata.search_tags_editable) {
                    result.metadata_search_tag_storage.reserve(tab.search_tags.size());
                    result.metadata_search_tag_length_storage.reserve(tab.search_tags.size());
                    for (const auto& tag : tab.search_tags) {
                        result.metadata_search_tag_storage.push_back(tag.c_str());
                        result.metadata_search_tag_length_storage.push_back(tag.size());
                        metadata.search_tags_editable =
                            metadata.search_tags_editable &&
                            tag.size() <= WORM_UI_METADATA_SEARCH_TAG_BYTES;
                    }
                }
                metadata.search_tags = metadata.search_tags_editable
                                           ? result.metadata_search_tag_storage.data()
                                           : nullptr;
                metadata.search_tag_bytes = metadata.search_tags_editable
                                                ? result.metadata_search_tag_length_storage.data()
                                                : nullptr;
                metadata.song_fields_editable =
                    metadata.song_fields_editable && metadata.search_tags_editable;
                metadata.has_chart = tab.selected_difficulty.has_value();
                metadata.audio_offset_editable = metadata.has_chart;
                if (tab.selected_difficulty) {
                    metadata.chart_id = tab.selected_difficulty->id.value;
                    metadata.chart_name = tab.selected_difficulty->difficulty_name.c_str();
                    metadata.chart_author = tab.chart_author.c_str();
                    metadata.chart_name_bytes = tab.selected_difficulty->difficulty_name.size();
                    metadata.chart_author_bytes = tab.chart_author.size();
                    metadata.chart_fields_editable =
                        tab.selected_difficulty->difficulty_name.size() <=
                            WORM_UI_METADATA_CHART_TEXT_BYTES &&
                        tab.chart_author.size() <= WORM_UI_METADATA_CHART_TEXT_BYTES;
                    metadata.has_chart_rating = tab.selected_difficulty->rating.has_value();
                    if (tab.selected_difficulty->rating) {
                        metadata.chart_rating_numerator =
                            tab.selected_difficulty->rating->numerator;
                        metadata.chart_rating_denominator =
                            tab.selected_difficulty->rating->denominator;
                    }
                    metadata.audio_offset_numerator = tab.beat_zero_audio_offset_seconds.numerator;
                    metadata.audio_offset_denominator =
                        tab.beat_zero_audio_offset_seconds.denominator;
                }
                metadata.format_fields_truncated =
                    tab.format_fields.size() > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY;
                metadata.format_field_total_count = tab.format_fields.size();
                metadata.format_field_count =
                    std::min(tab.format_fields.size(),
                             static_cast<std::size_t>(WORM_UI_METADATA_FORMAT_FIELD_CAPACITY));
                for (std::size_t index = 0; index < metadata.format_field_count; ++index) {
                    const auto& field = tab.format_fields[index];
                    result.metadata_format_field_storage[index] = {
                        .chart_scoped = field.chart_scoped,
                        .family = static_cast<std::uint8_t>(field.family),
                        .first_group_for_family = field.first_group_for_family,
                        .group_index = field.group_index,
                        .field_index = field.field_index,
                        .group_field_count = field.group_field_count,
                        .name = field.name.c_str(),
                        .value = field.value.c_str(),
                        .name_bytes = field.name.size(),
                        .value_bytes = field.value.size(),
                        .has_source_order = field.source_order.has_value(),
                        .source_order = field.source_order.value_or(0),
                    };
                }
                metadata.format_fields = result.metadata_format_field_storage.data();
                constexpr std::array format_families{
                    wormedit::format_family_t::native_project, wormedit::format_family_t::bms,
                    wormedit::format_family_t::stepmania,      wormedit::format_family_t::osu_mania,
                    wormedit::format_family_t::bmson,          wormedit::format_family_t::midi,
                    wormedit::format_family_t::quaver};
                const auto append_format_targets = [&](const auto& groups, bool chart_scoped) {
                    std::array<const wormedit::format_metadata_t*, format_families.size()>
                        first_groups{};
                    std::array<std::size_t, format_families.size()> first_group_indices{};
                    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                        const auto family_index =
                            static_cast<std::size_t>(groups[group_index].family);
                        if (family_index < first_groups.size() &&
                            first_groups[family_index] == nullptr) {
                            first_groups[family_index] = &groups[group_index];
                            first_group_indices[family_index] = group_index;
                        }
                    }
                    for (const auto family : format_families) {
                        const auto family_index = static_cast<std::size_t>(family);
                        const auto* found = first_groups[family_index];
                        const auto index = metadata.format_target_count++;
                        result.metadata_format_target_storage[index] = {
                            .chart_scoped = chart_scoped,
                            .family = static_cast<std::uint8_t>(family),
                            .group_exists = found != nullptr,
                            .group_index = found == nullptr ? 0 : first_group_indices[family_index],
                            .field_count = found == nullptr ? 0 : found->ordered_fields.size(),
                        };
                    }
                };
                append_format_targets(document.metadata.format_groups, false);
                if (tab.selected_difficulty) {
                    const auto selected =
                        std::ranges::find(document.charts, tab.selected_difficulty->id,
                                          &wormedit::chart_document_t::id);
                    if (selected != document.charts.end())
                        append_format_targets(selected->metadata.format_groups, true);
                }
                metadata.format_targets = result.metadata_format_target_storage.data();
                append("Song title: " + tab.title);
                append("Artist: " + tab.artist);
                append("Search tags: " + std::to_string(tab.search_tags.size()));
                append("Chart author: " +
                       (tab.chart_author.empty() ? std::string("(unset)") : tab.chart_author));
                append("Format-specific fields: " + std::to_string(tab.format_fields.size()));
                for (const auto& field : tab.format_fields) {
                    append(std::string(field.chart_scoped ? "Difficulty · " : "Song · ") +
                           std::string(format_family_name(field.family)) + " · " + field.name +
                           ": " + metadata_value_summary(field.value));
                }
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_timing_view_t>) {
                result.view.timing.visible = true;
                result.view.timing.measure_count = tab.measure_count;
                result.view.timing.bpm_count = tab.bpm_count;
                result.view.timing.pause_count = tab.pause_count;
                result.view.timing.warp_count = tab.warp_count;
                result.view.timing.bookmark_count = tab.bookmark_count;
                result.view.timing.section_count = tab.section_count;
                result.view.timing.anchor_count = tab.anchor_count;
                append("Measures: " + std::to_string(tab.measure_count));
                append("BPM events: " + std::to_string(tab.bpm_count));
                append("Stops / delays: " + std::to_string(tab.pause_count));
                append("Warps: " + std::to_string(tab.warp_count));
                append("Anchors: " + std::to_string(tab.anchor_count));
                append("Bookmarks / sections: " +
                       std::to_string(tab.bookmark_count + tab.section_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_compose_view_t>) {
                result.view.compose.visible = true;
                const auto& outline = tab.bms_branch_outline;
                auto& compose = result.view.compose;
                compose.branch_outline_available = outline.available;
                compose.branch_rows_truncated = outline.rows_truncated;
                compose.branch_display_text_truncated = outline.display_text_truncated;
                compose.branch_diagnostics_truncated = outline.diagnostics_truncated;
                compose.branch_row_count =
                    std::min(outline.rows.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY));
                compose.branch_rows_truncated =
                    compose.branch_rows_truncated || compose.branch_row_count < outline.rows.size();
                for (std::size_t row_index = 0; row_index < compose.branch_row_count; ++row_index) {
                    const auto& row = outline.rows[row_index];
                    result.compose_branch_text_storage[row_index] = row.display_text;
                    result.compose_branch_row_storage[row_index] = {
                        .display_text = result.compose_branch_text_storage[row_index].c_str(),
                        .depth = row.depth,
                        .active = row.active,
                        .selected = row.selected,
                        .display_text_truncated = row.display_text_truncated,
                        .has_source_order = row.source_order.has_value(),
                        .source_order = row.source_order.value_or(0),
                    };
                }
                compose.branch_rows = result.compose_branch_row_storage.data();
                compose.branch_diagnostic_count =
                    std::min(outline.diagnostics.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY));
                compose.branch_diagnostics_truncated =
                    compose.branch_diagnostics_truncated ||
                    compose.branch_diagnostic_count < outline.diagnostics.size();
                for (std::size_t diagnostic_index = 0;
                     diagnostic_index < compose.branch_diagnostic_count; ++diagnostic_index) {
                    auto& diagnostic = result.compose_branch_diagnostic_storage[diagnostic_index];
                    diagnostic = outline.diagnostics[diagnostic_index].message;
                    compose.branch_diagnostics[diagnostic_index] = diagnostic.c_str();
                }
                append("Measures: " + std::to_string(tab.measure_count));
                append("Notes: " + std::to_string(tab.note_count));
                append("Layout segments: " + std::to_string(tab.layout_segment_count));
                append("Visible lane definitions: " + std::to_string(tab.lane_count));
                append("Semantic layers: " + std::to_string(tab.layer_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_storyboard_view_t>) {
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_audio_slices_view_t>) {
                auto& audio = result.view.audio_slices;
                audio.visible = true;
                audio.assets_truncated = tab.audio_assets.size() > WORM_UI_AUDIO_ASSET_CAPACITY;
                audio.asset_count =
                    std::min(tab.audio_assets.size(),
                             static_cast<std::size_t>(WORM_UI_AUDIO_ASSET_CAPACITY));
                result.audio_asset_storage.reserve(audio.asset_count);
                for (std::size_t index = 0; index < audio.asset_count; ++index)
                    result.audio_asset_storage.push_back(tab.audio_assets[index].value);
                audio.asset_ids = result.audio_asset_storage.data();
                audio.has_selected_asset = tab.transport.audio_asset.has_value();
                audio.selected_asset_id =
                    tab.transport.audio_asset.value_or(wormedit::asset_id_t{}).value;
                append("Audio assets: " + std::to_string(tab.audio_assets.size()));
                append("Clips: " + std::to_string(tab.clip_count));
                append("Selected chart sound channels: " + std::to_string(tab.sound_channel_count));
                append(std::string("Transport: ") +
                       (tab.transport.playing ? "playing" : "stopped"));
            } else if constexpr (std::is_same_v<tab_t,
                                                wormedit::workspace_compare_validate_view_t>) {
                append("Compared difficulties: " + std::to_string(tab.comparison_charts.size()));
                append("Validation errors: " + std::to_string(tab.error_count));
                append("Validation warnings: " + std::to_string(tab.warning_count));
                for (const auto& diagnostic : tab.diagnostics) {
                    append(diagnostic.message);
                }
            }
        },
        source.tab_view);
    return result;
}

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double {
    float recovery_height = 0.0f;
    if (view.recovery_panel_visible) {
        recovery_height = 40.0f + (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                          static_cast<float>(view.recovery_candidate_count) * 48.0f +
                          static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
        for (std::size_t row = 0; row < view.recovery_candidate_count; ++row) {
            const auto& candidate = view.recovery_candidates[row];
            if ((candidate.action_mask & WORM_UI_RECOVERY_ACTION_SAVE_AS) != 0) {
                recovery_height += 25.0f;
                if (candidate.path_label == nullptr || candidate.path_label[0] == '\0')
                    recovery_height += 18.0f;
            }
            if (candidate.disabled_reason && candidate.disabled_reason[0])
                recovery_height += 20.0f;
        }
    }
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 165.0f - recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_row_count, 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count, 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_row_count > rows_shown ||
        compose.branch_diagnostic_count > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_row_count != 0 ||
                                compose.branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 23.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                (branch_status_shown ? 1 : 0)) *
                                                 18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}

constexpr double compose_measure_rail_width_pixels{42.0};
constexpr double compose_bgm_lane_width_pixels{36.0};

auto compose_bgm_pane_width(double viewport_width_pixels, bool has_bgm_lanes) -> double {
    if (!has_bgm_lanes)
        return 0.0;
    const auto preferred = std::clamp(viewport_width_pixels * 0.25, 96.0, 192.0);
    return std::min(
        preferred, std::max(0.0, viewport_width_pixels - compose_measure_rail_width_pixels - 80.0));
}

auto compose_playable_field_width(double viewport_width_pixels, double bgm_pane_width_pixels)
    -> double {
    return std::max(1.0, viewport_width_pixels - compose_measure_rail_width_pixels -
                             bgm_pane_width_pixels);
}

auto rational_as_long_double(wormedit::signed_rational_t value) -> long double {
    if (value.denominator == 0) {
        return 0.0L;
    }
    return static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}

auto rational_as_double(wormedit::rational_t value) -> double {
    if (value.denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

auto timing_position_at_row(const wormedit::measure_index_t& measures, std::uint64_t row)
    -> std::optional<wormedit::local_position_t> {
    const auto found = std::ranges::find_if(measures.entries, [row](const auto& entry) {
        return row >= entry.absolute_begin_row &&
               row - entry.absolute_begin_row < entry.row_capacity;
    });
    if (found == measures.entries.end()) {
        return std::nullopt;
    }
    return wormedit::local_position_t{.measure = found->id, .row = row - found->absolute_begin_row};
}

constexpr std::array<std::uint64_t, 10> chart_snap_preferences{4, 8, 6, 12, 16, 24, 32, 48, 64, 96};

auto requested_snap_denominator(const application_command_state_t& application_state,
                                wormedit::chart_id_t chart) -> std::uint64_t {
    const auto found = application_state.requested_snap_denominators.find(chart.value);
    return found == application_state.requested_snap_denominators.end() ? 4 : found->second;
}

void remember_snap_denominator(application_command_state_t& application_state,
                               wormedit::chart_id_t chart, std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        application_state.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

auto refresh_timing_grid(const wormedit::chart_document_t& chart,
                         application_command_state_t& application_state) -> timing_grid_state_t {
    const auto snap = wormedit::filter_snap_denominators(
        chart.divisions_per_standard_measure, chart_snap_preferences,
        requested_snap_denominator(application_state, chart.id));
    timing_grid_state_t state{.chart = chart.id,
                              .supported_denominators = snap.supported_denominators,
                              .active_denominator = snap.active_denominator,
                              .active_was_fallback = snap.active_was_fallback};
    // Preserve a selected valid denominator across timing gestures, while an
    // incompatible chart safely uses the exact one-row fallback.
    if (snap && !snap.inserted_resolution_fallback) {
        remember_snap_denominator(application_state, chart.id, snap.active_denominator);
    }
    return state;
}

auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    if (active_denominator == 0 || chart.divisions_per_standard_measure % active_denominator != 0) {
        return 0;
    }
    return chart.divisions_per_standard_measure / active_denominator;
}

auto timing_position_from_fraction(const wormedit::chart_document_t& chart, double fraction,
                                   std::uint64_t active_denominator)
    -> std::optional<wormedit::local_position_t> {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0) {
        return std::nullopt;
    }
    const auto measures_result = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&measures_result);
    if (!measures || measures->total_rows == 0) {
        return std::nullopt;
    }
    const auto step = timing_stage_grid_step(chart, active_denominator);
    if (step == 0) {
        return std::nullopt;
    }
    const auto raw_row_value =
        std::min(std::floor(static_cast<long double>(fraction) * measures->total_rows),
                 static_cast<long double>(measures->total_rows - 1));
    const auto raw_row = static_cast<std::uint64_t>(raw_row_value);
    const auto remainder = raw_row % step;
    auto snapped_row = raw_row - remainder;
    if (remainder >= step - remainder &&
        snapped_row <= std::numeric_limits<std::uint64_t>::max() - step) {
        snapped_row += step;
    }
    const auto last_valid_row = measures->total_rows - 1;
    if (snapped_row > last_valid_row) {
        snapped_row = last_valid_row - last_valid_row % step;
    }
    return timing_position_at_row(*measures, snapped_row);
}

auto timing_lane_kind_from_ui(WormUiTimingLaneKind lane)
    -> std::optional<wormedit::timing_lane_kind_t> {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM:
        return kind_t::bpm;
    case WORM_UI_TIMING_LANE_SCROLL:
        return kind_t::scroll;
    case WORM_UI_TIMING_LANE_SPEED:
        return kind_t::speed;
    case WORM_UI_TIMING_LANE_STOP:
        return kind_t::stop;
    case WORM_UI_TIMING_LANE_DELAY:
        return kind_t::delay;
    case WORM_UI_TIMING_LANE_WARP:
        return kind_t::warp;
    case WORM_UI_TIMING_LANE_KIAI:
        return kind_t::kiai;
    case WORM_UI_TIMING_LANE_ANCHOR:
        return kind_t::anchor;
    }
    return std::nullopt;
}

auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case kind_t::bpm:
        return WORM_UI_TIMING_LANE_BPM;
    case kind_t::scroll:
        return WORM_UI_TIMING_LANE_SCROLL;
    case kind_t::speed:
        return WORM_UI_TIMING_LANE_SPEED;
    case kind_t::stop:
        return WORM_UI_TIMING_LANE_STOP;
    case kind_t::delay:
        return WORM_UI_TIMING_LANE_DELAY;
    case kind_t::warp:
        return WORM_UI_TIMING_LANE_WARP;
    case kind_t::kiai:
        return WORM_UI_TIMING_LANE_KIAI;
    case kind_t::anchor:
        return WORM_UI_TIMING_LANE_ANCHOR;
    }
    return WORM_UI_TIMING_LANE_BPM;
}

auto timing_pause_unit_from_ui(WormUiTimingPauseUnit unit)
    -> std::optional<wormedit::pause_unit_t> {
    using unit_t = wormedit::pause_unit_t;
    switch (unit) {
    case WORM_UI_TIMING_PAUSE_SECONDS:
        return unit_t::seconds;
    case WORM_UI_TIMING_PAUSE_MILLISECONDS:
        return unit_t::milliseconds;
    case WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS:
        return unit_t::bms_stop_ticks;
    case WORM_UI_TIMING_PAUSE_BEATS:
        return unit_t::beats;
    }
    return std::nullopt;
}

auto timing_pause_unit_to_ui(wormedit::pause_unit_t unit) -> WormUiTimingPauseUnit {
    using unit_t = wormedit::pause_unit_t;
    switch (unit) {
    case unit_t::seconds:
        return WORM_UI_TIMING_PAUSE_SECONDS;
    case unit_t::milliseconds:
        return WORM_UI_TIMING_PAUSE_MILLISECONDS;
    case unit_t::bms_stop_ticks:
        return WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS;
    case unit_t::beats:
        return WORM_UI_TIMING_PAUSE_BEATS;
    }
    return WORM_UI_TIMING_PAUSE_SECONDS;
}

auto begin_timing_stage(const WormUiWorkspaceAction& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_preview_t {
    using namespace wormedit;
    timing_lane_pseudonote_preview_t rejected{};
    if (workspace.active_tab != workspace_tab_t::timing || !workspace.selected_chart ||
        action.timing_chart_id == 0 || workspace.selected_chart->value != action.timing_chart_id ||
        history.document.revision != action.timing_document_revision) {
        return rejected;
    }
    const auto requested_kind = timing_lane_kind_from_ui(action.timing_lane);
    if (!requested_kind) {
        return rejected;
    }
    if (action.timing_object_id != 0) {
        auto preview = begin_existing_timing_lane_pseudonote(
            history, chart_id_t{action.timing_chart_id}, *requested_kind,
            object_id_t{action.timing_object_id}, timing_locked);
        if (preview.status == timing_lane_pseudonote_status_t::editing &&
            action.timing_has_target_y) {
            const auto chart =
                std::ranges::find(history.document.charts, preview.chart, &chart_document_t::id);
            const auto target =
                chart == history.document.charts.end()
                    ? std::optional<local_position_t>{}
                    : timing_position_from_fraction(*chart, action.timing_target_y_fraction,
                                                    active_snap_denominator);
            if (!target ||
                !move_existing_timing_lane_pseudonote(history, preview, *target, timing_locked)) {
                return rejected;
            }
        }
        return preview;
    }
    if (!action.timing_has_target_y) {
        return rejected;
    }
    const auto found = std::ranges::find(history.document.charts,
                                         chart_id_t{action.timing_chart_id}, &chart_document_t::id);
    if (found == history.document.charts.end()) {
        return rejected;
    }
    const auto position = timing_position_from_fraction(*found, action.timing_target_y_fraction,
                                                        active_snap_denominator);
    if (!position) {
        return rejected;
    }

    const auto kind = *requested_kind;
    timing_lane_value_t initial{timing_bpm_value_t{}};
    switch (kind) {
    case timing_lane_kind_t::bpm:
        initial = timing_bpm_value_t{{120, 1}};
        break;
    case timing_lane_kind_t::scroll:
        initial = timing_scroll_value_t{{1, 1}};
        break;
    case timing_lane_kind_t::speed:
        initial = timing_speed_value_t{{1, 1}, {0, 1}, false};
        break;
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        initial = timing_pause_value_t{{1, 1}, pause_unit_t::seconds};
        break;
    case timing_lane_kind_t::warp: {
        const auto measures_result = build_measure_index(*found);
        const auto* measures = std::get_if<measure_index_t>(&measures_result);
        const auto step = timing_stage_grid_step(*found, active_snap_denominator);
        const auto start = measures ? absolute_row(*measures, *position) : std::nullopt;
        if (!measures || !start || step == 0 ||
            step > std::numeric_limits<std::uint64_t>::max() - *start ||
            *start + step >= measures->total_rows) {
            return rejected;
        }
        const auto end = timing_position_at_row(*measures, *start + step);
        if (!end) {
            return rejected;
        }
        initial = timing_warp_value_t{*end, warp_provenance_t::native};
        break;
    }
    case timing_lane_kind_t::kiai:
        initial = timing_kiai_value_t{true, 0};
        break;
    case timing_lane_kind_t::anchor:
        initial = timing_anchor_value_t{0, false};
        break;
    }
    return begin_timing_lane_pseudonote(
        history, chart_id_t{action.timing_chart_id}, kind, *position, std::move(initial),
        timing_lane_initial_value_source_t::explicit_default, timing_locked);
}

auto parse_timing_signed(const char* text) -> std::optional<std::int64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::int64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto parse_timing_unsigned(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto timing_ratio_in_bounds(std::int64_t numerator, std::uint64_t denominator, long double minimum,
                            long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    const auto value = static_cast<long double>(numerator) / denominator;
    return value >= minimum && value <= maximum;
}

auto timing_unsigned_ratio_in_bounds(std::uint64_t numerator, std::uint64_t denominator,
                                     long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    return static_cast<long double>(numerator) / denominator <= maximum;
}

auto timing_stage_value(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        const wormedit::chart_document_t& chart,
                        const WormUiWorkspaceAction& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const auto primary_numerator = parse_timing_signed(action.timing_primary_numerator_text);
    const auto primary_unsigned = parse_timing_unsigned(action.timing_primary_numerator_text);
    const auto primary_denominator = parse_timing_unsigned(action.timing_primary_denominator_text);
    const auto secondary_numerator = parse_timing_signed(action.timing_secondary_numerator_text);
    const auto secondary_denominator =
        parse_timing_unsigned(action.timing_secondary_denominator_text);
    switch (preview.kind) {
    case timing_lane_kind_t::bpm: {
        if (action.timing_bpm_step != 0) {
            const auto* current = std::get_if<timing_bpm_value_t>(&preview.value);
            const auto direction =
                action.timing_bpm_step == -1  ? std::optional{bpm_step_direction_t::decrement}
                : action.timing_bpm_step == 1 ? std::optional{bpm_step_direction_t::increment}
                                              : std::nullopt;
            const auto stepped = current && direction
                                     ? step_bpm_by_one(current->beats_per_minute, *direction)
                                     : std::nullopt;
            if (!stepped ||
                (!preview.existing_event &&
                 !timing_ratio_in_bounds(stepped->numerator, stepped->denominator, 1.0L, 1000.0L)))
                return std::nullopt;
            return timing_lane_value_t{timing_bpm_value_t{*stepped}};
        }
        if (action.timing_bpm_decimal_edited) {
            const auto bpm = parse_bpm_text(action.timing_bpm_decimal_text);
            if (!bpm || (!preview.existing_event &&
                         !timing_ratio_in_bounds(bpm->numerator, bpm->denominator, 1.0L, 1000.0L)))
                return std::nullopt;
            return timing_lane_value_t{timing_bpm_value_t{*bpm}};
        }
        if (!primary_numerator || !primary_denominator || *primary_numerator <= 0 ||
            (!preview.existing_event &&
             !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 1.0L, 1000.0L)))
            return std::nullopt;
        return timing_lane_value_t{timing_bpm_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::scroll: {
        if (!primary_numerator || !primary_denominator ||
            (!preview.existing_event &&
             !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, -1000.0L, 1000.0L)))
            return std::nullopt;
        return timing_lane_value_t{
            timing_scroll_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::speed: {
        if (!primary_numerator || !primary_denominator || !secondary_numerator ||
            *secondary_numerator < 0 || !secondary_denominator || *primary_numerator < 0 ||
            (!preview.existing_event &&
             (!timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 0.0L, 1000.0L) ||
              !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*secondary_numerator),
                                               *secondary_denominator, 10000.0L)))) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_speed_value_t{
            {*primary_numerator, *primary_denominator},
            {static_cast<std::uint64_t>(*secondary_numerator), *secondary_denominator},
            action.timing_transition_is_seconds}};
    }
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        const auto source_unit = timing_pause_unit_from_ui(action.timing_pause_unit);
        const auto maximum_duration = [&]() -> long double {
            if (!source_unit) {
                return 0.0L;
            }
            switch (*source_unit) {
            case pause_unit_t::seconds:
                return 86'400.0L;
            case pause_unit_t::milliseconds:
                return 86'400'000.0L;
            case pause_unit_t::bms_stop_ticks:
            case pause_unit_t::beats:
                return 1'000'000.0L;
            }
            return 0.0L;
        }();
        if (!primary_numerator || !primary_denominator || *primary_numerator < 0 || !source_unit ||
            (!preview.existing_event &&
             !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*primary_numerator),
                                              *primary_denominator, maximum_duration))) {
            return std::nullopt;
        }
        return timing_lane_value_t{
            timing_pause_value_t{{*primary_numerator, *primary_denominator}, *source_unit}};
    }
    case timing_lane_kind_t::warp: {
        if (!secondary_numerator || *secondary_numerator < 1 || !secondary_denominator ||
            *secondary_denominator != 1 ||
            (!preview.existing_event && *secondary_numerator > 1'000'000)) {
            return std::nullopt;
        }
        const auto units = static_cast<std::uint64_t>(*secondary_numerator);
        const auto measures = build_measure_index(chart);
        const auto* index = std::get_if<measure_index_t>(&measures);
        const auto start = index ? absolute_row(*index, preview.position) : std::nullopt;
        const auto step = preview.existing_event.has_value()
                              ? std::uint64_t{1}
                              : timing_stage_grid_step(chart, active_snap_denominator);
        if (!index || !start || step == 0 ||
            units > (std::numeric_limits<std::uint64_t>::max() - *start) / step) {
            return std::nullopt;
        }
        const auto end_row = *start + units * step;
        if (end_row >= index->total_rows) {
            return std::nullopt;
        }
        const auto end = timing_position_at_row(*index, end_row);
        return end ? std::optional<timing_lane_value_t>{timing_warp_value_t{
                         *end, warp_provenance_t::native}}
                   : std::nullopt;
    }
    case timing_lane_kind_t::kiai:
        return timing_lane_value_t{
            timing_kiai_value_t{action.timing_toggle_primary,
                                std::get<timing_kiai_value_t>(preview.value).retained_effect_bits}};
    case timing_lane_kind_t::anchor:
        if (!primary_unsigned || !primary_denominator || *primary_denominator != 1) {
            return std::nullopt;
        }
        return timing_lane_value_t{
            timing_anchor_value_t{*primary_unsigned, action.timing_toggle_primary}};
    }
    return std::nullopt;
}

auto parse_recipe_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<wormedit::signed_rational_t> {
    const auto numerator = parse_timing_signed(text.numerator);
    const auto denominator = parse_timing_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) {
        return std::nullopt;
    }
    return wormedit::signed_rational_t{*numerator, *denominator};
}

auto parse_recipe_unsigned_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<wormedit::rational_t> {
    const auto numerator = parse_timing_unsigned(text.numerator);
    const auto denominator = parse_timing_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) {
        return std::nullopt;
    }
    return wormedit::rational_t{*numerator, *denominator};
}

auto recipe_candidate_from_ui(const WormUiTimingRecipeForm& form,
                              const wormedit::sv_recipe_t& current)
    -> std::optional<wormedit::sv_recipe_t> {
    using namespace wormedit;
    if (form.output < WORM_UI_TIMING_RECIPE_OUTPUT_SV ||
        form.output > WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS ||
        form.curve < WORM_UI_TIMING_RECIPE_CURVE_CONSTANT ||
        form.curve > WORM_UI_TIMING_RECIPE_CURVE_BUMP ||
        form.sampling_mode < WORM_UI_TIMING_RECIPE_SAMPLING_FIXED ||
        form.sampling_mode > WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE ||
        form.fixed_extent < WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT ||
        form.fixed_extent > WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS) {
        return std::nullopt;
    }
    auto candidate = current;
    candidate.output = static_cast<sv_recipe_output_t>(form.output);
    candidate.curve = static_cast<sv_recipe_curve_t>(form.curve);
    const auto set_parameter = [](signed_rational_t& target,
                                  const WormUiTimingRecipeRationalText& source) {
        const auto parsed = parse_recipe_rational(source);
        if (parsed) {
            target = *parsed;
        }
        return parsed.has_value();
    };
    switch (candidate.curve) {
    case sv_recipe_curve_t::constant:
        if (!set_parameter(candidate.parameters.value_from, form.value_from)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::linear:
        if (!set_parameter(candidate.parameters.value_from, form.value_from) ||
            !set_parameter(candidate.parameters.value_to, form.value_to)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::exponential:
        if (!set_parameter(candidate.parameters.value_from, form.value_from) ||
            !set_parameter(candidate.parameters.value_to, form.value_to) ||
            !set_parameter(candidate.parameters.shape, form.shape)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::bump:
        if (!set_parameter(candidate.parameters.bump_start, form.bump_start) ||
            !set_parameter(candidate.parameters.bump_length, form.bump_length) ||
            !set_parameter(candidate.parameters.bump_x, form.bump_x) ||
            !set_parameter(candidate.parameters.bump_y, form.bump_y)) {
            return std::nullopt;
        }
        break;
    }
    candidate.sampling.mode = static_cast<sv_recipe_sampling_mode_t>(form.sampling_mode);
    if (candidate.sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
            const auto count = parse_timing_unsigned(form.fixed_point_count);
            if (!count || *count > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            candidate.sampling.fixed_point_count = static_cast<std::uint32_t>(*count);
            candidate.sampling.fixed_step_rows.reset();
        } else {
            const auto rows = parse_timing_unsigned(form.fixed_step_rows);
            if (!rows) {
                return std::nullopt;
            }
            candidate.sampling.fixed_point_count.reset();
            candidate.sampling.fixed_step_rows = *rows;
        }
    } else {
        const auto tolerance = parse_recipe_rational(form.tolerance);
        const auto max_points = parse_timing_unsigned(form.adaptive_max_points);
        if (!tolerance || !max_points || *max_points > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        candidate.sampling.fixed_point_count.reset();
        candidate.sampling.fixed_step_rows.reset();
        candidate.sampling.tolerance = *tolerance;
        candidate.sampling.max_points = static_cast<std::uint32_t>(*max_points);
    }
    if (candidate.output == sv_recipe_output_t::speeds) {
        const auto transition = parse_recipe_unsigned_rational(form.transition_duration);
        if (!transition) {
            return std::nullopt;
        }
        candidate.transition_duration = *transition;
        candidate.transition_is_seconds = form.transition_is_seconds;
    } else {
        candidate.transition_duration.reset();
        candidate.transition_is_seconds.reset();
    }
    return candidate;
}

auto timing_recipe_range_hits(const wormedit::chart_document_t& chart, double begin_fraction,
                              double end_fraction)
    -> std::optional<std::pair<wormedit::local_position_t, wormedit::local_position_t>> {
    if (!std::isfinite(begin_fraction) || !std::isfinite(end_fraction) || begin_fraction < 0.0 ||
        begin_fraction > 1.0 || end_fraction < 0.0 || end_fraction > 1.0) {
        return std::nullopt;
    }
    const auto built = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    if (!index || index->total_rows < 2) {
        return std::nullopt;
    }
    const auto row_at = [&](double fraction) {
        if (fraction >= 1.0) {
            return index->total_rows;
        }
        const auto scaled = std::floor(static_cast<long double>(fraction) *
                                       static_cast<long double>(index->total_rows));
        return std::min(static_cast<std::uint64_t>(std::max(0.0L, scaled)), index->total_rows - 1);
    };
    const auto position_at = [&](std::uint64_t row) -> std::optional<wormedit::local_position_t> {
        if (row == index->total_rows && !index->entries.empty()) {
            const auto& last = index->entries.back();
            return wormedit::local_position_t{last.id, last.row_capacity};
        }
        return timing_position_at_row(*index, row);
    };
    const auto begin = position_at(row_at(begin_fraction));
    const auto end = position_at(row_at(end_fraction));
    return begin && end ? std::optional{std::pair{*begin, *end}} : std::nullopt;
}

void set_recipe_rational_text(WormUiTimingRecipeRationalText& target,
                              wormedit::signed_rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%lld",
                        static_cast<long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

void set_recipe_rational_text(WormUiTimingRecipeRationalText& target, wormedit::rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%llu",
                        static_cast<unsigned long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

void populate_timing_recipe_view(flattened_workspace_t& flattened,
                                 const wormedit::chart_document_t& chart,
                                 const wormedit::measure_index_t* measures,
                                 const wormedit::sv_recipe_draft_t* draft,
                                 const wormedit::sv_recipe_draft_preview_t* preview,
                                 bool selection_armed) {
    auto& view = flattened.view.timing.sv_recipe;
    view.selection_armed = selection_armed;
    view.chart_id = chart.id.value;
    view.document_revision = flattened.view.timing.document_revision;
    if (!draft || draft->status != wormedit::sv_recipe_draft_status_t::editing || !measures ||
        measures->total_rows == 0) {
        return;
    }
    view.active = true;
    view.chart_id = draft->chart.value;
    view.document_revision = draft->document_revision;
    view.recipe_id = draft->existing_recipe ? draft->existing_recipe->value : 0;
    const auto begin_row = wormedit::absolute_row(*measures, draft->candidate.range.begin);
    auto end_row = wormedit::absolute_row(*measures, draft->candidate.range.end);
    if (!end_row && !measures->entries.empty()) {
        const auto& last = measures->entries.back();
        if (draft->candidate.range.end.measure == last.id &&
            draft->candidate.range.end.row == last.row_capacity) {
            end_row = measures->total_rows;
        }
    }
    if (begin_row && end_row) {
        view.range_begin_fraction = static_cast<double>(
            static_cast<long double>(*begin_row) / static_cast<long double>(measures->total_rows));
        view.range_end_fraction = static_cast<double>(
            static_cast<long double>(*end_row) / static_cast<long double>(measures->total_rows));
    }

    const auto& candidate = draft->candidate;
    auto& form = view.form;
    form.output = static_cast<WormUiTimingRecipeOutput>(candidate.output);
    form.curve = static_cast<WormUiTimingRecipeCurve>(candidate.curve);
    form.sampling_mode = static_cast<WormUiTimingRecipeSamplingMode>(candidate.sampling.mode);
    form.fixed_extent = candidate.sampling.fixed_step_rows
                            ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                            : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
    set_recipe_rational_text(form.value_from, candidate.parameters.value_from);
    set_recipe_rational_text(form.value_to, candidate.parameters.value_to);
    set_recipe_rational_text(form.shape, candidate.parameters.shape);
    set_recipe_rational_text(form.bump_start, candidate.parameters.bump_start);
    set_recipe_rational_text(form.bump_length, candidate.parameters.bump_length);
    set_recipe_rational_text(form.bump_x, candidate.parameters.bump_x);
    set_recipe_rational_text(form.bump_y, candidate.parameters.bump_y);
    set_recipe_rational_text(form.tolerance, candidate.sampling.tolerance);
    set_recipe_rational_text(form.transition_duration,
                             candidate.transition_duration.value_or(wormedit::rational_t{}));
    form.transition_is_seconds = candidate.transition_is_seconds.value_or(false);
    (void)std::snprintf(form.fixed_point_count, sizeof(form.fixed_point_count), "%u",
                        candidate.sampling.fixed_point_count.value_or(2));
    (void)std::snprintf(
        form.fixed_step_rows, sizeof(form.fixed_step_rows), "%llu",
        static_cast<unsigned long long>(candidate.sampling.fixed_step_rows.value_or(1)));
    (void)std::snprintf(form.adaptive_max_points, sizeof(form.adaptive_max_points), "%u",
                        candidate.sampling.max_points);

    if (!preview) {
        flattened.timing_recipe_status_storage = "The transient recipe preview is unavailable.";
        view.status_text = flattened.timing_recipe_status_storage.c_str();
        return;
    }
    view.valid = preview->diagnostics.empty();
    if (!preview->diagnostics.empty()) {
        flattened.timing_recipe_status_storage = preview->diagnostics.front().message;
    } else if (!preview->candidate_points.tolerance_met) {
        flattened.timing_recipe_status_storage =
            "Adaptive sampling reached its point cap before meeting tolerance.";
    } else {
        flattened.timing_recipe_status_storage =
            "Preview is transient; Apply stores only the editable recipe.";
    }
    view.status_text = flattened.timing_recipe_status_storage.c_str();

    long double minimum = 0.0L;
    long double maximum = 1.0L;
    const auto include_values = [&](const auto& points) {
        for (const auto& point : points) {
            if (std::isfinite(point.value)) {
                minimum = std::min(minimum, static_cast<long double>(point.value));
                maximum = std::max(maximum, static_cast<long double>(point.value));
            }
        }
    };
    include_values(preview->candidate_points.points);
    include_values(preview->effective_output.points);
    const auto scale = maximum > minimum ? maximum - minimum : 1.0L;
    const auto append_points = [&](const auto& source, auto& destination) {
        destination.reserve(
            std::min<std::size_t>(source.size(), WORM_UI_TIMING_RECIPE_POINT_CAPACITY));
        const auto append = [&](std::size_t source_index) {
            const auto& point = source[source_index];
            const auto row_fraction = static_cast<long double>(point.absolute_row) /
                                      static_cast<long double>(measures->total_rows);
            const auto value_fraction = (static_cast<long double>(point.value) - minimum) / scale;
            destination.push_back({
                .row_fraction = static_cast<double>(std::clamp(row_fraction, 0.0L, 1.0L)),
                .value_fraction = static_cast<double>(std::clamp(value_fraction, 0.0L, 1.0L)),
            });
        };
        if (source.size() <= WORM_UI_TIMING_RECIPE_POINT_CAPACITY) {
            for (std::size_t index = 0; index < source.size(); ++index) {
                append(index);
            }
            return;
        }

        // Keep the first/last point plus the first, last, minimum, and maximum
        // value in each vertical row bucket. This bounded screen-oriented
        // decimator preserves narrow extrema and structural boundaries that
        // uniform index skipping can make disappear.
        constexpr std::size_t bucket_count = (WORM_UI_TIMING_RECIPE_POINT_CAPACITY - 2) / 4;
        const auto bucket_for = [&](std::size_t index) {
            const auto fraction = static_cast<long double>(source[index].absolute_row) /
                                  static_cast<long double>(measures->total_rows);
            return std::min(static_cast<std::size_t>(fraction * bucket_count), bucket_count - 1);
        };
        append(0);
        for (std::size_t first = 1; first + 1 < source.size();) {
            const auto bucket = bucket_for(first);
            auto after = first + 1;
            auto minimum_index = first;
            auto maximum_index = first;
            while (after + 1 < source.size() && bucket_for(after) == bucket) {
                if (source[after].value < source[minimum_index].value) {
                    minimum_index = after;
                }
                if (source[after].value > source[maximum_index].value) {
                    maximum_index = after;
                }
                ++after;
            }
            std::array candidates{first, minimum_index, maximum_index, after - 1};
            std::ranges::sort(candidates);
            const auto unique_end = std::ranges::unique(candidates).begin();
            for (auto candidate = candidates.begin(); candidate != unique_end; ++candidate) {
                append(*candidate);
            }
            first = after;
        }
        append(source.size() - 1);
    };
    append_points(preview->candidate_points.points, flattened.timing_recipe_candidate_storage);
    append_points(preview->effective_output.points, flattened.timing_recipe_composed_storage);
    view.candidate_points = flattened.timing_recipe_candidate_storage.data();
    view.candidate_point_count = flattened.timing_recipe_candidate_storage.size();
    view.composed_points = flattened.timing_recipe_composed_storage.data();
    view.composed_point_count = flattened.timing_recipe_composed_storage.size();
}

void populate_timing_view(flattened_workspace_t& flattened,
                          const wormedit::document_history_t& history,
                          const wormedit::workspace_controller_t& workspace,
                          timing_scene_cache_t& cache, int window_width, int window_height,
                          const timing_grid_state_t& timing_grid,
                          const wormedit::timing_lane_pseudonote_preview_t* stage,
                          const wormedit::sv_recipe_draft_t* recipe_draft,
                          const wormedit::sv_recipe_draft_preview_t* recipe_preview,
                          bool recipe_selection_armed) {
    auto& view = flattened.view.timing;
    if (!view.visible) {
        return;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    if (!workspace.selected_chart) {
        flattened.timing_status_storage = "Choose a difficulty to display its timing lanes.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.timing_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    view.active_snap_denominator = timing_grid.active_denominator;
    view.supported_snap_denominators = timing_grid.supported_denominators.data();
    view.supported_snap_denominator_count = timing_grid.supported_denominators.size();
    view.speed_total_count = chart.speeds.size();
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache = {.chart = chart.id, .revision = history.document.revision, .attempted = true};
        const auto timing = wormedit::build_timing_index(chart);
        if (!timing.index) {
            cache.status = timing.diagnostics.empty() ? "The timing index could not be built."
                                                      : timing.diagnostics.front().message;
        } else {
            const auto scroll_speed = wormedit::build_scroll_speed_index(chart, *timing.index);
            if (!scroll_speed.index) {
                cache.status = scroll_speed.diagnostics.empty()
                                   ? "The SPEEDS evaluator could not be built."
                                   : scroll_speed.diagnostics.front().message;
            } else {
                cache.total_rows = scroll_speed.index->total_rows;
                const auto spans = wormedit::scroll_speed_transition_spans(
                    *scroll_speed.index, *timing.index,
                    {.maximum_spans = WORM_UI_TIMING_SPEED_CAPACITY});
                if (!spans) {
                    cache.status = spans.diagnostics.empty()
                                       ? "The SPEEDS transition spans could not be built."
                                       : spans.diagnostics.front().message;
                }
                cache.truncated = spans.truncated;
                cache.speeds.reserve(spans.spans.size());
                std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>>
                    speed_orders;
                speed_orders.reserve(chart.speeds.size());
                for (std::size_t storage_order = 0; storage_order < chart.speeds.size();
                     ++storage_order) {
                    const auto& event = chart.speeds[storage_order];
                    speed_orders.emplace(
                        event.id.value,
                        std::pair{static_cast<std::uint64_t>(event.provenance.source_order),
                                  static_cast<std::uint64_t>(storage_order)});
                }
                for (std::size_t index = 0; index < spans.spans.size(); ++index) {
                    const auto& speed = scroll_speed.index->speed_segments[index];
                    const auto& span = spans.spans[index];
                    cache.speeds.push_back({
                        .object_id = speed.source_event.value,
                        .source_order = speed_orders[speed.source_event.value].first,
                        .storage_order = speed_orders[speed.source_event.value].second,
                        .marker_row = static_cast<long double>(speed.begin_row),
                        .affected_begin_row = rational_as_long_double(span.begin_row),
                        .affected_end_row = rational_as_long_double(span.end_row),
                        .multiplier =
                            static_cast<double>(rational_as_long_double(speed.target_multiplier)),
                        .transition_duration = rational_as_double(speed.transition_duration),
                        .transition_is_seconds = speed.transition_is_seconds,
                        .transition_clipped = span.clipped,
                    });
                }
            }
        }
    }
    if (!cache.status.empty()) {
        flattened.timing_status_storage = cache.status;
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    if (cache.total_rows == 0) {
        flattened.timing_status_storage = "This difficulty has no timing rows to display.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    view.total_rows = cache.total_rows;

    const auto row_to_pixels = [&](long double row) {
        const auto bounded = std::clamp(row, 0.0L, static_cast<long double>(cache.total_rows));
        return static_cast<double>(bounded / static_cast<long double>(cache.total_rows) *
                                   view.viewport_height_pixels);
    };
    flattened.timing_grid_storage.clear();
    const auto grid = wormedit::make_editor_grid_lines(
        chart,
        {.visible_begin_row = 0,
         .visible_end_row = cache.total_rows,
         .active_snap_denominator = timing_grid.active_denominator,
         .pixels_per_standard_measure = view.viewport_height_pixels *
                                        static_cast<double>(chart.divisions_per_standard_measure) /
                                        static_cast<double>(cache.total_rows),
         .maximum_line_count = WORM_UI_TIMING_GRID_CAPACITY});
    if (!grid) {
        flattened.timing_status_storage = grid.diagnostics.empty()
                                              ? "The timing grid could not be composed."
                                              : grid.diagnostics.front().message;
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    flattened.timing_grid_storage.reserve(grid.lines.size());
    for (const auto& line : grid.lines) {
        const auto row = static_cast<long double>(line.chart_row.whole_rows) +
                         static_cast<long double>(line.chart_row.fractional_rows.numerator) /
                             line.chart_row.fractional_rows.denominator;
        flattened.timing_grid_storage.push_back(
            {.y_pixels = row_to_pixels(row),
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis)});
    }
    flattened.timing_speed_storage.reserve(cache.speeds.size());
    for (const auto& speed : cache.speeds) {
        flattened.timing_speed_storage.push_back({
            .object_id = speed.object_id,
            .source_order = speed.source_order,
            .storage_order = speed.storage_order,
            .marker_y_pixels = row_to_pixels(speed.marker_row),
            .affected_begin_y_pixels = row_to_pixels(speed.affected_begin_row),
            .affected_end_y_pixels = row_to_pixels(speed.affected_end_row),
            .multiplier = speed.multiplier,
            .transition_duration = speed.transition_duration,
            .transition_is_seconds = speed.transition_is_seconds,
            .transition_clipped = speed.transition_clipped,
        });
    }
    std::ranges::sort(flattened.timing_speed_storage, [](const auto& left, const auto& right) {
        if (left.marker_y_pixels != right.marker_y_pixels) {
            return left.marker_y_pixels < right.marker_y_pixels;
        }
        if (left.source_order != right.source_order) {
            return left.source_order < right.source_order;
        }
        return left.storage_order != right.storage_order ? left.storage_order < right.storage_order
                                                         : left.object_id < right.object_id;
    });
    for (std::size_t first = 0; first < flattened.timing_speed_storage.size();) {
        auto after = first + 1;
        while (after < flattened.timing_speed_storage.size() &&
               flattened.timing_speed_storage[after].marker_y_pixels ==
                   flattened.timing_speed_storage[first].marker_y_pixels) {
            ++after;
        }
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            flattened.timing_speed_storage[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            flattened.timing_speed_storage[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
    flattened.timing_marker_storage.clear();
    const auto measure_result = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&measure_result);
    std::size_t marker_reserve = 0;
    for (const auto count : {chart.bpms.size(), chart.scrolls.size(), chart.pauses.size(),
                             chart.warps.size(), chart.kiai.size(), chart.timing_anchors.size()}) {
        marker_reserve +=
            std::min<std::size_t>(count, WORM_UI_TIMING_MARKER_CAPACITY - marker_reserve);
    }
    flattened.timing_marker_storage.reserve(marker_reserve);
    if (measures) {
        const auto append_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                       std::size_t source_order, std::size_t storage_order,
                                       WormUiTimingLaneKind lane) {
            if (flattened.timing_marker_storage.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
                view.truncated = true;
                return;
            }
            const auto row = wormedit::absolute_row(*measures, position);
            if (!row) {
                return;
            }
            flattened.timing_marker_storage.push_back({
                .object_id = id,
                .source_order = source_order,
                .storage_order = storage_order,
                .marker_y_pixels = row_to_pixels(static_cast<long double>(*row)),
                .lane = lane,
            });
        };
        for (std::size_t index = 0; index < chart.bpms.size(); ++index)
            append_marker(chart.bpms[index].position, chart.bpms[index].id.value,
                          chart.bpms[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_BPM);
        for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
            append_marker(chart.scrolls[index].position, chart.scrolls[index].id.value,
                          chart.scrolls[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_SCROLL);
        for (std::size_t index = 0; index < chart.pauses.size(); ++index)
            append_marker(chart.pauses[index].position, chart.pauses[index].id.value,
                          chart.pauses[index].provenance.source_order, index,
                          chart.pauses[index].kind == wormedit::pause_kind_t::stop
                              ? WORM_UI_TIMING_LANE_STOP
                              : WORM_UI_TIMING_LANE_DELAY);
        for (std::size_t index = 0; index < chart.warps.size(); ++index)
            append_marker(chart.warps[index].span.begin, chart.warps[index].id.value,
                          chart.warps[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_WARP);
        for (std::size_t index = 0; index < chart.kiai.size(); ++index)
            append_marker(chart.kiai[index].position, chart.kiai[index].id.value,
                          chart.kiai[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_KIAI);
        for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
            append_marker(chart.timing_anchors[index].position,
                          chart.timing_anchors[index].id.value, index, index,
                          WORM_UI_TIMING_LANE_ANCHOR);
        std::ranges::sort(flattened.timing_marker_storage, [](const auto& left, const auto& right) {
            if (left.lane != right.lane) {
                return left.lane < right.lane;
            }
            if (left.marker_y_pixels != right.marker_y_pixels) {
                return left.marker_y_pixels < right.marker_y_pixels;
            }
            if (left.source_order != right.source_order) {
                return left.source_order < right.source_order;
            }
            return left.storage_order != right.storage_order
                       ? left.storage_order < right.storage_order
                       : left.object_id < right.object_id;
        });
        for (std::size_t first = 0; first < flattened.timing_marker_storage.size();) {
            auto after = first + 1;
            while (after < flattened.timing_marker_storage.size() &&
                   flattened.timing_marker_storage[after].lane ==
                       flattened.timing_marker_storage[first].lane &&
                   flattened.timing_marker_storage[after].marker_y_pixels ==
                       flattened.timing_marker_storage[first].marker_y_pixels) {
                ++after;
            }
            const auto count = static_cast<double>(after - first);
            for (auto index = first; index < after; ++index) {
                flattened.timing_marker_storage[index].marker_left_fraction =
                    static_cast<double>(index - first) / count;
                flattened.timing_marker_storage[index].marker_right_fraction =
                    static_cast<double>(index - first + 1) / count;
            }
            first = after;
        }
    }
    view.scene_available = true;
    view.truncated = view.truncated || cache.truncated;
    view.speeds = flattened.timing_speed_storage.data();
    view.speed_count = flattened.timing_speed_storage.size();
    view.markers = flattened.timing_marker_storage.data();
    view.marker_count = flattened.timing_marker_storage.size();
    view.grid_lines = flattened.timing_grid_storage.data();
    view.grid_line_count = flattened.timing_grid_storage.size();
    populate_timing_recipe_view(flattened, chart, measures, recipe_draft, recipe_preview,
                                recipe_selection_armed);
    if (stage && stage->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        stage->chart == chart.id && stage->document_revision == history.document.revision) {
        view.stage_active = true;
        view.stage_valid = stage->valid;
        view.stage_existing_object_id =
            stage->existing_event.has_value() ? stage->existing_event->value : 0;
        view.stage_lane = timing_lane_kind_to_ui(stage->kind);
        const auto stage_row =
            measures ? wormedit::absolute_row(*measures, stage->position) : std::nullopt;
        view.stage_marker_y_pixels =
            stage_row ? row_to_pixels(static_cast<long double>(*stage_row)) : 0.0;
        view.stage_transition_is_seconds = false;
        view.stage_toggle_primary = false;
        view.stage_pause_unit = WORM_UI_TIMING_PAUSE_SECONDS;
        std::visit(
            [&](const auto& value) {
                using value_t = std::decay_t<decltype(value)>;
                const auto set_primary = [&](std::int64_t numerator, std::uint64_t denominator) {
                    (void)std::snprintf(view.stage_primary_numerator_text,
                                        sizeof(view.stage_primary_numerator_text), "%lld",
                                        static_cast<long long>(numerator));
                    (void)std::snprintf(view.stage_primary_denominator_text,
                                        sizeof(view.stage_primary_denominator_text), "%llu",
                                        static_cast<unsigned long long>(denominator));
                };
                const auto set_secondary = [&](std::uint64_t numerator, std::uint64_t denominator) {
                    (void)std::snprintf(view.stage_secondary_numerator_text,
                                        sizeof(view.stage_secondary_numerator_text), "%llu",
                                        static_cast<unsigned long long>(numerator));
                    (void)std::snprintf(view.stage_secondary_denominator_text,
                                        sizeof(view.stage_secondary_denominator_text), "%llu",
                                        static_cast<unsigned long long>(denominator));
                };
                if constexpr (std::is_same_v<value_t, wormedit::timing_bpm_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.beats_per_minute));
                    set_primary(value.beats_per_minute.numerator,
                                value.beats_per_minute.denominator);
                    if (const auto formatted = wormedit::format_bpm_text(value.beats_per_minute)) {
                        (void)std::snprintf(view.stage_primary_decimal_text,
                                            sizeof(view.stage_primary_decimal_text), "%s",
                                            formatted->decimal.c_str());
                        view.stage_primary_decimal_exact = formatted->decimal_exact;
                    }
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_scroll_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_speed_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    view.stage_secondary_value = rational_as_double(value.transition_beats);
                    view.stage_transition_is_seconds = value.transition_is_seconds;
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                    set_secondary(value.transition_beats.numerator,
                                  value.transition_beats.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_pause_value_t>) {
                    view.stage_value = static_cast<double>(rational_as_long_double(value.duration));
                    view.stage_pause_unit = timing_pause_unit_to_ui(value.source_unit);
                    set_primary(value.duration.numerator, value.duration.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_warp_value_t>) {
                    const auto end_row =
                        measures ? wormedit::absolute_row(*measures, value.end) : std::nullopt;
                    const auto start_row = stage_row;
                    const auto step =
                        stage->existing_event.has_value()
                            ? std::uint64_t{1}
                            : timing_stage_grid_step(chart, timing_grid.active_denominator);
                    const auto length_steps =
                        end_row && start_row && *end_row > *start_row
                            ? static_cast<long double>(*end_row - *start_row) /
                                  static_cast<long double>(step)
                            : 1.0L;
                    view.stage_secondary_value = static_cast<double>(length_steps);
                    (void)std::snprintf(view.stage_secondary_numerator_text,
                                        sizeof(view.stage_secondary_numerator_text), "%.0Lf",
                                        length_steps);
                    (void)std::snprintf(view.stage_secondary_denominator_text,
                                        sizeof(view.stage_secondary_denominator_text), "1");
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_kiai_value_t>) {
                    view.stage_toggle_primary = value.enabled;
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_anchor_value_t>) {
                    view.stage_toggle_primary = value.locked;
                    (void)std::snprintf(view.stage_primary_numerator_text,
                                        sizeof(view.stage_primary_numerator_text), "%llu",
                                        static_cast<unsigned long long>(value.source_sample_frame));
                    (void)std::snprintf(view.stage_primary_denominator_text,
                                        sizeof(view.stage_primary_denominator_text), "1");
                }
            },
            stage->value);
        if (!stage->diagnostics.empty()) {
            flattened.timing_status_storage = stage->diagnostics.front().message;
            view.stage_status_text = flattened.timing_status_storage.c_str();
        }
    }
}

auto refresh_timing_recipe_preview(const wormedit::document_history_t& history,
                                   const wormedit::sv_recipe_draft_t& draft,
                                   std::optional<wormedit::sv_recipe_draft_preview_t>& preview,
                                   std::string& status) -> bool {
    preview = wormedit::preview_sv_recipe_draft(history, draft);
    if (!preview->diagnostics.empty()) {
        status = preview->diagnostics.front().message;
        return false;
    }
    status = "SV recipe preview updated; Apply is the only durable step.";
    return true;
}

auto dispatch_timing_recipe_action(
    const WormUiTimingRecipeAction& action, wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator,
    std::optional<std::pair<wormedit::chart_id_t, std::uint64_t>>& selection_binding,
    std::optional<wormedit::sv_recipe_draft_t>& draft,
    std::optional<wormedit::sv_recipe_draft_preview_t>& preview, std::string& status) -> bool {
    using namespace wormedit;
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        return false;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION) {
        if (timing_locked || workspace.active_tab != workspace_tab_t::timing ||
            !workspace.selected_chart) {
            status = timing_locked ? "Unlock timing before selecting an SV recipe range."
                                   : "SV recipe selection requires the active Timing chart.";
            return true;
        }
        if (draft) {
            status = "Apply or cancel the current SV recipe before selecting another interval.";
            return true;
        }
        selection_binding = std::pair{*workspace.selected_chart, history.document.revision};
        status = "Drag over the Scroll / SV lane to select the recipe interval.";
        return true;
    }

    const bool binding_matches = workspace.active_tab == workspace_tab_t::timing &&
                                 workspace.selected_chart &&
                                 workspace.selected_chart->value == action.chart_id &&
                                 history.document.revision == action.document_revision;
    if (!binding_matches) {
        if (draft) {
            cancel_sv_recipe_draft(*draft);
        }
        draft.reset();
        preview.reset();
        selection_binding.reset();
        status = "SV recipe editing expired because its chart or revision changed.";
        return true;
    }
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end()) {
        status = "The selected chart is no longer available.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN) {
        if (timing_locked) {
            status = "Unlock timing before adding an SV recipe.";
            return true;
        }
        if (draft) {
            status = "Apply or cancel the current SV recipe before starting another.";
            return true;
        }
        if (!selection_binding || selection_binding->first != chart->id ||
            selection_binding->second != action.document_revision) {
            selection_binding.reset();
            status = "SV recipe range selection expired before the drag began.";
            return true;
        }
        const auto hits = timing_recipe_range_hits(*chart, action.range_begin_fraction,
                                                   action.range_end_fraction);
        if (!hits) {
            status = "The selected SV recipe interval is outside the chart grid.";
            selection_binding.reset();
            return true;
        }
        auto candidate = sv_recipe_t{
            .name = "SV function",
            .range = {hits->first, hits->second},
            .output = sv_recipe_output_t::sv,
            .curve = sv_recipe_curve_t::linear,
        };
        auto next = begin_sv_recipe_draft(history, chart->id, std::move(candidate), timing_locked);
        if (next.status != sv_recipe_draft_status_t::editing ||
            !set_sv_recipe_draft_range(history, next, hits->first, hits->second,
                                       active_snap_denominator, timing_locked)) {
            status = next.diagnostics.empty() ? "The selected interval cannot form an SV recipe."
                                              : next.diagnostics.front().message;
            selection_binding.reset();
            return true;
        }
        draft = std::move(next);
        selection_binding.reset();
        (void)refresh_timing_recipe_preview(history, *draft, preview, status);
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_CANCEL && !draft) {
        selection_binding.reset();
        preview.reset();
        status = "SV recipe range selection cancelled.";
        return true;
    }
    if (!draft || draft->chart != chart->id ||
        draft->document_revision != action.document_revision) {
        status = "There is no matching SV recipe draft to update.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_CANCEL) {
        cancel_sv_recipe_draft(*draft);
        draft.reset();
        preview.reset();
        selection_binding.reset();
        status = "SV recipe editing cancelled; the chart was not changed.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE) {
        const auto hits = timing_recipe_range_hits(*chart, action.range_begin_fraction,
                                                   action.range_end_fraction);
        if (!hits ||
            !set_sv_recipe_draft_range(history, *draft, hits ? hits->first : local_position_t{},
                                       hits ? hits->second : local_position_t{},
                                       active_snap_denominator, timing_locked)) {
            status = !hits || draft->diagnostics.empty()
                         ? "The selected interval cannot form an SV recipe."
                         : draft->diagnostics.front().message;
            return true;
        }
        const auto candidate = recipe_candidate_from_ui(action.form, draft->candidate);
        if (!candidate) {
            preview = sv_recipe_draft_preview_t{};
            preview->diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = diagnostic_code_t::invalid_rational,
                .message = "Enter bounded exact recipe parameters and sampling values.",
            });
            status = preview->diagnostics.front().message;
            return true;
        }
        if (!update_sv_recipe_draft(history, *draft, *candidate, timing_locked)) {
            status = draft->diagnostics.empty() ? "The SV recipe draft could not be updated."
                                                : draft->diagnostics.front().message;
            return true;
        }
        (void)refresh_timing_recipe_preview(history, *draft, preview, status);
        return true;
    }

    const auto candidate = recipe_candidate_from_ui(action.form, draft->candidate);
    if (!candidate) {
        preview = sv_recipe_draft_preview_t{};
        preview->diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_rational,
            .message = "Enter bounded exact recipe parameters and sampling values.",
        });
        status = preview->diagnostics.front().message;
        return true;
    }
    if (!update_sv_recipe_draft(history, *draft, *candidate, timing_locked)) {
        status = draft->diagnostics.empty() ? "The SV recipe draft could not be updated."
                                            : draft->diagnostics.front().message;
        return true;
    }
    const bool preview_valid = refresh_timing_recipe_preview(history, *draft, preview, status);
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_APPLY) {
        if (!preview_valid) {
            return true;
        }
        const auto result = apply_sv_recipe_draft(history, *draft, timing_locked);
        if (!result.diagnostics.empty()) {
            status = result.diagnostics.front().message;
            return true;
        }
        const bool edited_existing = draft->existing_recipe.has_value();
        status = edited_existing ? "SV recipe updated." : "SV recipe added.";
        draft.reset();
        preview.reset();
        selection_binding.reset();
    }
    return true;
}

auto compose_lane_is_editable(const wormedit::lane_definition_t& lane) -> bool {
    using wormedit::lane_role_t;
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto compose_physical_scancode(wormedit::physical_key_t key) -> std::uint32_t {
    using wormedit::physical_key_t;
    switch (key) {
    case physical_key_t::digit_1:
        return SDL_SCANCODE_1;
    case physical_key_t::digit_2:
        return SDL_SCANCODE_2;
    case physical_key_t::digit_3:
        return SDL_SCANCODE_3;
    case physical_key_t::digit_4:
        return SDL_SCANCODE_4;
    case physical_key_t::digit_5:
        return SDL_SCANCODE_5;
    case physical_key_t::digit_6:
        return SDL_SCANCODE_6;
    case physical_key_t::digit_7:
        return SDL_SCANCODE_7;
    case physical_key_t::digit_8:
        return SDL_SCANCODE_8;
    case physical_key_t::digit_9:
        return SDL_SCANCODE_9;
    case physical_key_t::digit_0:
        return SDL_SCANCODE_0;
    case physical_key_t::key_q:
        return SDL_SCANCODE_Q;
    case physical_key_t::key_w:
        return SDL_SCANCODE_W;
    case physical_key_t::key_e:
        return SDL_SCANCODE_E;
    case physical_key_t::key_r:
        return SDL_SCANCODE_R;
    case physical_key_t::key_t:
        return SDL_SCANCODE_T;
    case physical_key_t::key_y:
        return SDL_SCANCODE_Y;
    case physical_key_t::key_u:
        return SDL_SCANCODE_U;
    case physical_key_t::key_i:
        return SDL_SCANCODE_I;
    case physical_key_t::key_o:
        return SDL_SCANCODE_O;
    case physical_key_t::key_p:
        return SDL_SCANCODE_P;
    case physical_key_t::key_a:
        return SDL_SCANCODE_A;
    case physical_key_t::key_s:
        return SDL_SCANCODE_S;
    case physical_key_t::key_d:
        return SDL_SCANCODE_D;
    case physical_key_t::key_f:
        return SDL_SCANCODE_F;
    case physical_key_t::key_g:
        return SDL_SCANCODE_G;
    case physical_key_t::key_h:
        return SDL_SCANCODE_H;
    case physical_key_t::key_j:
        return SDL_SCANCODE_J;
    case physical_key_t::key_k:
        return SDL_SCANCODE_K;
    case physical_key_t::key_l:
        return SDL_SCANCODE_L;
    case physical_key_t::key_z:
        return SDL_SCANCODE_Z;
    case physical_key_t::key_x:
        return SDL_SCANCODE_X;
    case physical_key_t::key_c:
        return SDL_SCANCODE_C;
    case physical_key_t::key_v:
        return SDL_SCANCODE_V;
    case physical_key_t::key_b:
        return SDL_SCANCODE_B;
    case physical_key_t::key_n:
        return SDL_SCANCODE_N;
    case physical_key_t::key_m:
        return SDL_SCANCODE_M;
    case physical_key_t::unknown:
        return 0;
    }
    return 0;
}

void populate_compose_view(flattened_workspace_t& flattened,
                           const wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state,
                           application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!view.visible) {
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        return;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.playable_field_right_pixels = view.viewport_width_pixels;
    view.pixels_per_standard_measure = retained_state
                                           ? retained_state->pixels_per_standard_measure
                                           : wormedit::default_pixels_per_standard_measure;
    view.note_paint_mode_active = application_state.note_paint_mode_active;
    view.semantic_layer_paint_mode_active = application_state.semantic_layer_paint_mode_active;
    view.selected_note_count = 0;
    view.semantic_layers = flattened.compose_semantic_layer_storage.data();
    view.semantic_layer_count = 0;
    view.semantic_layer_total_count = 0;
    view.semantic_layer_page = 0;
    view.semantic_layer_page_count = 0;
    view.semantic_layers_truncated = false;
    view.semantic_layer_target_locked = false;
    view.semantic_layer_target_visible = false;
    view.semantic_layer_target_required_filter = false;
    view.semantic_layer_target_excluded_filter = false;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.status_text = nullptr;
    view.selected_note_label = nullptr;
    view.selected_note_label_note_id = 0;
    view.focus_note_label_editor = false;
    view.hover_note_visible = false;
    view.hover_note_id = 0;
    view.hover_tooltip = nullptr;
    view.viewport_autoscroll_active = cache.viewport_autoscroll_active;
    view.viewport_autoscroll_anchor_x_pixels = cache.viewport_autoscroll_anchor_x_pixels;
    view.viewport_autoscroll_anchor_y_pixels = cache.viewport_autoscroll_anchor_y_pixels;
    view.viewport_autoscroll_pointer_x_pixels = cache.viewport_autoscroll_pointer_x_pixels;
    view.viewport_autoscroll_pointer_y_pixels = cache.viewport_autoscroll_pointer_y_pixels;
    flattened.compose_hover_tooltip_storage.clear();
    flattened.compose_selected_note_label_storage.clear();
    if (!workspace.selected_chart) {
        flattened.compose_status_storage = "Choose a difficulty to display its chart field.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.compose_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    if (application_state.semantic_layer_filter_chart != chart.id) {
        application_state.semantic_layer_filter_chart = chart.id;
        application_state.required_semantic_layer_filters.clear();
        application_state.excluded_semantic_layer_filters.clear();
    }
    if (application_state.semantic_layer_target &&
        std::ranges::find(chart.semantic_layers, *application_state.semantic_layer_target,
                          &wormedit::semantic_layer_t::id) == chart.semantic_layers.end()) {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
    }
    view.semantic_layer_paint_mode_active = application_state.semantic_layer_paint_mode_active;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache.semantic_layer_membership_counts.clear();
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                for (const auto layer : note.semantic_layers) {
                    auto& count = cache.semantic_layer_membership_counts[layer.value];
                    if (count != std::numeric_limits<std::size_t>::max()) {
                        ++count;
                    }
                }
            }
        }
    }
    const auto palette_context_changed = application_state.semantic_layer_palette_chart != chart.id;
    const auto palette_target_changed =
        application_state.semantic_layer_palette_target != application_state.semantic_layer_target;
    if (palette_context_changed) {
        application_state.semantic_layer_palette_chart = chart.id;
        application_state.semantic_layer_palette_page = 0;
    }
    const auto palette = wormedit::make_semantic_layer_palette_window(
        chart, application_state.semantic_layer_target,
        application_state.semantic_layer_palette_page,
        palette_context_changed || palette_target_changed, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
    if (!palette) {
        flattened.compose_status_storage = "The semantic-layer palette exceeds its UI limit.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    application_state.semantic_layer_palette_target = application_state.semantic_layer_target;
    application_state.semantic_layer_palette_page = palette->page;
    view.semantic_layer_total_count = palette->total_count;
    view.semantic_layer_page = palette->page;
    view.semantic_layer_page_count = palette->page_count;
    view.semantic_layers_truncated = palette->page_count > 1;
    for (const auto layer_index : palette->layer_indices) {
        const auto& layer = chart.semantic_layers[layer_index];
        const auto selected = application_state.semantic_layer_target == layer.id;
        const auto index = view.semantic_layer_count++;
        flattened.compose_semantic_layer_name_storage[index] = layer.name;
        flattened.compose_semantic_layer_storage[index] = {
            .layer_id = layer.id.value,
            .name = flattened.compose_semantic_layer_name_storage[index].c_str(),
            .red = layer.color.red,
            .green = layer.color.green,
            .blue = layer.color.blue,
            .alpha = layer.color.alpha,
            .assigned_note_count = cache.semantic_layer_membership_counts[layer.id.value],
            .visible = layer.visible,
            .locked = layer.locked,
            .selected = selected,
        };
        view.semantic_layer_target_locked =
            view.semantic_layer_target_locked || (selected && layer.locked);
        view.semantic_layer_target_visible =
            view.semantic_layer_target_visible || (selected && layer.visible);
        view.semantic_layer_target_required_filter =
            view.semantic_layer_target_required_filter ||
            (selected &&
             std::ranges::find(application_state.required_semantic_layer_filters, layer.id) !=
                 application_state.required_semantic_layer_filters.end());
        view.semantic_layer_target_excluded_filter =
            view.semantic_layer_target_excluded_filter ||
            (selected &&
             std::ranges::find(application_state.excluded_semantic_layer_filters, layer.id) !=
                 application_state.excluded_semantic_layer_filters.end());
    }
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache.index.reset();
        cache.hit_test_index.reset();
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        cache.status.clear();
        cache.chart = chart.id;
        cache.revision = history.document.revision;
        cache.attempted = true;
        const auto built =
            wormedit::build_editor_field_scene_index(chart, history.document.revision);
        if (std::holds_alternative<wormedit::editor_field_scene_index_t>(built)) {
            cache.index = std::get<wormedit::editor_field_scene_index_t>(built);
        } else {
            const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(built);
            cache.status = diagnostics.empty() ? "The field layout could not be indexed."
                                               : diagnostics.front().message;
        }
    }
    if (!cache.index) {
        flattened.compose_status_storage = cache.status;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    if (chart.measures.empty()) {
        flattened.compose_status_storage = "This difficulty has no measures to display.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }

    const auto measure_index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(measure_index)) {
        const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(measure_index);
        flattened.compose_status_storage = diagnostics.empty()
                                               ? "The chart measure geometry is invalid."
                                               : diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    if (!retained_state || retained_state->chart != chart.id) {
        std::optional<wormedit::local_position_t> initial_cursor;
        std::optional<wormedit::lane_id_t> initial_lane;
        std::uint64_t earliest_layout_row = std::numeric_limits<std::uint64_t>::max();
        for (const auto& segment : chart.lane_layouts) {
            const auto row = wormedit::absolute_row(
                std::get<wormedit::measure_index_t>(measure_index), segment.position);
            if (!row || *row >= earliest_layout_row)
                continue;
            const auto lane = std::ranges::find_if(segment.layout.lanes, compose_lane_is_editable);
            if (lane == segment.layout.lanes.end())
                continue;
            earliest_layout_row = *row;
            initial_cursor = segment.position;
            initial_lane = lane->id;
        }
        if (!initial_cursor || !initial_lane) {
            flattened.compose_status_storage = "This difficulty has no editable lanes to display.";
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        const auto initial = wormedit::make_editor_interaction(
            history, chart.id, *initial_lane, *initial_cursor, chart_snap_preferences,
            requested_snap_denominator(application_state, chart.id), view.viewport_height_pixels);
        if (!initial.state || !initial.view) {
            flattened.compose_status_storage = initial.diagnostics.empty()
                                                   ? "The chart field view could not be created."
                                                   : initial.diagnostics.front().message;
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state = *initial.state;
    }

    // Compose and Timing consume the same chart-scoped snap preference. Do
    // not replace a gesture's captured grid mid-drag: the next idle frame
    // applies a Timing selection or a Compose snap-cycle uniformly.
    if (!editor_interaction_has_pending_gesture(*retained_state)) {
        const auto snap = wormedit::filter_snap_denominators(
            chart.divisions_per_standard_measure, chart_snap_preferences,
            requested_snap_denominator(application_state, chart.id));
        if (!snap) {
            flattened.compose_status_storage = "The chart snap configuration is invalid.";
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state->supported_snap_denominators = snap.supported_denominators;
        retained_state->active_snap_denominator = snap.active_denominator;
        retained_state->active_snap_was_fallback = snap.active_was_fallback;
    }

    retained_state->viewport_height_pixels = view.viewport_height_pixels;
    auto interaction = wormedit::make_editor_interaction(
        history, chart.id, retained_state->active_lane, retained_state->cursor,
        retained_state->supported_snap_denominators, retained_state->active_snap_denominator,
        view.viewport_height_pixels);
    if (!interaction.view) {
        // Undo, recovery, or an upstream chart edit may invalidate a retained cursor.
        retained_state.reset();
        flattened.compose_status_storage = "The retained chart cursor is no longer valid.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    auto interaction_view = *interaction.view;
    interaction_view.pixels_per_standard_measure = retained_state->pixels_per_standard_measure;
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    if (application_state.preview_playhead_absolute_row) {
        const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                    static_cast<double>(chart.divisions_per_standard_measure);
        const auto playhead_y = *application_state.preview_playhead_absolute_row * pixels_per_row -
                                interaction_view.viewport_scroll_pixels;
        if (std::isfinite(playhead_y) && playhead_y >= 0.0 &&
            playhead_y <= view.viewport_height_pixels) {
            view.playhead_visible = true;
            view.playhead_y_pixels = playhead_y;
        }
    }
    interaction_view.pending_hold = retained_state->pending_hold;
    interaction_view.pending_note_duration_gesture = retained_state->pending_note_duration_gesture;
    interaction_view.pending_note_move_gesture = retained_state->pending_note_move_gesture;
    interaction_view.pending_note_paint_gesture =
        retained_state->pending_note_paint_gesture
            ? std::optional{wormedit::note_paint_gesture_preview_t{
                  .document_revision =
                      retained_state->pending_note_paint_gesture->document_revision,
                  .targets = retained_state->pending_note_paint_gesture->targets,
              }}
            : std::nullopt;
    interaction_view.pending_editor_area_selection =
        retained_state->pending_editor_area_selection
            ? std::optional{wormedit::editor_area_selection_preview_t{
                  .document_revision =
                      retained_state->pending_editor_area_selection->document_revision,
                  .left_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .top_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .right_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .bottom_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .notes = retained_state->pending_editor_area_selection->notes,
                  .objects = retained_state->pending_editor_area_selection->objects,
              }}
            : std::nullopt;
    interaction_view.selected_notes = retained_state->selected_notes;
    interaction_view.selected_objects = retained_state->selected_objects;
    view.selected_note_count = retained_state->selected_notes.size();
    if (retained_state->selected_notes.size() == 1) {
        const auto note_id = retained_state->selected_notes.front();
        for (const auto& measure : chart.measures) {
            const auto note = std::ranges::find(measure.notes, note_id, &wormedit::note_t::id);
            if (note == measure.notes.end()) {
                continue;
            }
            if (note->label.has_value()) {
                flattened.compose_selected_note_label_storage = *note->label;
            }
            view.selected_note_label = flattened.compose_selected_note_label_storage.c_str();
            view.selected_note_label_note_id = note_id.value;
            break;
        }
    }
    const auto content_height = static_cast<long double>(cache.index->measures.total_rows) *
                                interaction_view.pixels_per_standard_measure /
                                chart.divisions_per_standard_measure;
    const auto maximum_scroll =
        std::max(0.0L, content_height - static_cast<long double>(view.viewport_height_pixels));
    retained_state->viewport_scroll_pixels = std::clamp(retained_state->viewport_scroll_pixels, 0.0,
                                                        static_cast<double>(maximum_scroll));
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    view.pixels_per_standard_measure = interaction_view.pixels_per_standard_measure;
    cache.interaction_view = interaction_view;
    const auto hover_gesture_active =
        retained_state->pending_hold.has_value() ||
        retained_state->pending_note_duration_gesture.has_value() ||
        retained_state->pending_note_move_gesture.has_value() ||
        retained_state->pending_note_paint_gesture.has_value() ||
        retained_state->pending_semantic_layer_paint_gesture.has_value() ||
        retained_state->pending_editor_area_selection.has_value() ||
        retained_state->pending_space_range_selection.has_value();
    if (!hover_gesture_active && cache.hover &&
        cache.hover->document_revision == history.document.revision &&
        cache.hover->object.kind == wormedit::editor_selection_object_kind_t::note) {
        view.hover_note_visible = true;
        view.hover_note_id = cache.hover->object.id.value;
        flattened.compose_hover_tooltip_storage = cache.hover->tooltip;
        view.hover_tooltip = flattened.compose_hover_tooltip_storage.c_str();
    }
    if (!hover_gesture_active && cache.focus_note_label_editor &&
        retained_state->selected_notes.size() == 1) {
        view.focus_note_label_editor = true;
    }
    const auto scalar_selection_mode = [](wormedit::editor_area_selection_mode_t mode) {
        switch (mode) {
        case wormedit::editor_area_selection_mode_t::replace:
            return WORM_UI_COMPOSE_SELECTION_REPLACE;
        case wormedit::editor_area_selection_mode_t::add:
            return WORM_UI_COMPOSE_SELECTION_ADD;
        case wormedit::editor_area_selection_mode_t::toggle:
            return WORM_UI_COMPOSE_SELECTION_TOGGLE;
        }
        return WORM_UI_COMPOSE_SELECTION_REPLACE;
    };
    if (interaction_view.pending_editor_area_selection) {
        const auto& preview = *interaction_view.pending_editor_area_selection;
        view.area_selection_preview_visible = true;
        view.area_selection_preview_mode =
            scalar_selection_mode(retained_state->pending_editor_area_selection->mode);
        view.area_selection_preview_bounds = {
            std::clamp(preview.left_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(preview.top_pixels, 0.0, view.viewport_height_pixels),
            std::clamp(preview.right_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(preview.bottom_pixels, 0.0, view.viewport_height_pixels),
        };
    }
    if (retained_state->pending_space_range_selection) {
        const auto& pending = *retained_state->pending_space_range_selection;
        const auto& measures = std::get<wormedit::measure_index_t>(measure_index);
        const auto anchor_row = wormedit::absolute_row(measures, pending.anchor);
        const auto cursor_row = wormedit::absolute_row(measures, retained_state->cursor);
        if (anchor_row && cursor_row) {
            const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                        static_cast<double>(chart.divisions_per_standard_measure);
            const auto first_row = std::min(*anchor_row, *cursor_row);
            const auto last_row = std::max(*anchor_row, *cursor_row);
            const auto top = static_cast<double>(first_row) * pixels_per_row -
                             interaction_view.viewport_scroll_pixels;
            const auto bottom = static_cast<double>(last_row + 1) * pixels_per_row -
                                interaction_view.viewport_scroll_pixels;
            view.space_range_preview_visible = true;
            view.space_range_preview_mode = scalar_selection_mode(pending.mode);
            view.space_range_preview_bounds = {
                0.0,
                std::clamp(top, 0.0, view.viewport_height_pixels),
                view.viewport_width_pixels,
                std::clamp(bottom, 0.0, view.viewport_height_pixels),
            };
            const auto row_center = [&](std::uint64_t row) {
                return std::clamp((static_cast<double>(row) + 0.5) * pixels_per_row -
                                      interaction_view.viewport_scroll_pixels,
                                  0.0, view.viewport_height_pixels);
            };
            view.space_range_anchor_y_pixels = row_center(*anchor_row);
            view.space_range_tail_y_pixels = row_center(*cursor_row);
        }
    }
    flattened.compose_interaction_view_storage = interaction_view;
    std::size_t lane_key_binding_count{};
    bool lane_key_bindings_truncated{};
    for (const auto& binding : chart.input_profile.bindings) {
        const auto scancode = compose_physical_scancode(binding.key);
        if (scancode == 0) {
            continue;
        }
        if (lane_key_binding_count >= flattened.compose_lane_key_storage.size()) {
            lane_key_bindings_truncated = true;
            break;
        }
        flattened.compose_lane_key_storage[lane_key_binding_count++] = {
            .scancode = scancode,
            .lane_id = binding.lane.value,
        };
    }
    view.lane_key_bindings = flattened.compose_lane_key_storage.data();
    view.lane_key_binding_count = lane_key_binding_count;

    const auto bgm_pane_width =
        compose_bgm_pane_width(view.viewport_width_pixels, !chart.bgm_source_lanes.empty());
    const auto playable_field_width =
        compose_playable_field_width(view.viewport_width_pixels, bgm_pane_width);
    view.playable_field_right_pixels = compose_measure_rail_width_pixels + playable_field_width;
    const auto maximum_bgm_scroll = std::max(
        0.0, static_cast<double>(chart.bgm_source_lanes.size()) * compose_bgm_lane_width_pixels -
                 bgm_pane_width);
    auto& bgm_horizontal_scroll = application_state.bgm_horizontal_scroll_pixels[chart.id.value];
    bgm_horizontal_scroll = std::clamp(bgm_horizontal_scroll, 0.0, maximum_bgm_scroll);
    const auto composed = wormedit::make_editor_field_scene(
        chart, *cache.index, interaction_view,
        {.field_left_pixels = compose_measure_rail_width_pixels,
         .field_width_pixels = playable_field_width,
         .split_stage_gutter_pixels = 14.0,
         .bgm_pane_left_pixels = compose_measure_rail_width_pixels + playable_field_width,
         .bgm_pane_width_pixels = bgm_pane_width,
         .bgm_lane_width_pixels = compose_bgm_lane_width_pixels,
         .bgm_horizontal_scroll_pixels = bgm_horizontal_scroll,
         .solo_semantic_layer = application_state.semantic_layer_solo_active
                                    ? application_state.semantic_layer_target
                                    : std::nullopt,
         .required_semantic_layers = application_state.required_semantic_layer_filters,
         .excluded_semantic_layers = application_state.excluded_semantic_layer_filters,
         .maximum_grid_lines = WORM_UI_COMPOSE_GRID_CAPACITY,
         .maximum_scene_primitives = WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY});
    if (!composed.scene || composed.has_errors()) {
        flattened.compose_status_storage = composed.diagnostics.empty()
                                               ? "The chart field could not be composed."
                                               : composed.diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& scene = *composed.scene;
    view.scene_available = true;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bands = scene.layout_bands;
    const auto band_count =
        std::min(bands.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BAND_CAPACITY));
    flattened.compose_band_storage.reserve(band_count);
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        flattened.compose_band_storage.push_back(
            {.bounds = {compose_measure_rail_width_pixels, band.top_pixels,
                        compose_measure_rail_width_pixels + playable_field_width,
                        band.bottom_pixels},
             .split_stage = band.split_stage});
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        std::min(lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_LANE_CAPACITY));
    flattened.compose_lane_storage.reserve(lane_count);
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        flattened.compose_lane_storage.push_back({.bounds = convert_rect(lane.bounds),
                                                  .lane_id = lane.lane.value,
                                                  .display_order = lane.display_order,
                                                  .role = static_cast<std::uint8_t>(lane.role),
                                                  .stage = static_cast<std::uint8_t>(lane.stage),
                                                  .glyph = static_cast<std::uint8_t>(lane.glyph)});
    }
    const auto& lines = scene.grid_lines;
    const auto line_count =
        std::min(lines.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_GRID_CAPACITY));
    std::unordered_map<std::uint64_t, std::uint32_t> measure_numbers;
    measure_numbers.reserve(cache.index->measures.entries.size());
    for (std::size_t measure = 0; measure < cache.index->measures.entries.size(); ++measure) {
        measure_numbers.emplace(cache.index->measures.entries[measure].absolute_begin_row,
                                static_cast<std::uint32_t>(measure + 1));
    }
    flattened.compose_grid_storage.reserve(line_count);
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        const auto measure = measure_numbers.find(line.chart_row.whole_rows);
        flattened.compose_grid_storage.push_back(
            {.y_pixels = line.y_pixels,
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis),
             .measure_number = line.kind == wormedit::editor_grid_line_kind_t::measure &&
                                       measure != measure_numbers.end()
                                   ? measure->second
                                   : 0});
    }
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        std::min(differences.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_DIFFERENCE_CAPACITY));
    flattened.compose_difference_storage.reserve(difference_count);
    for (std::size_t index = 0; index < difference_count; ++index) {
        flattened.compose_difference_storage.push_back(
            {.bounds = convert_rect(differences[index].bounds)});
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        std::min(holds.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_HOLD_CAPACITY));
    flattened.compose_hold_storage.reserve(hold_count);
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        flattened.compose_hold_storage.push_back(
            {.bounds = convert_rect(hold.bounds),
             .note_id = hold.note.value,
             .lane_id = hold.lane.value,
             .selected = hold.selected,
             .proposal_origin = hold.proposal_origin,
             .proposal_ghost = hold.proposal_ghost,
             .semantic_color_visible = hold.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0)});
    }
    const auto& endpoints = scene.note_endpoints;
    const auto endpoint_count =
        std::min(endpoints.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    flattened.compose_note_storage.reserve(endpoint_count);
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        flattened.compose_note_storage.push_back(
            {.bounds = convert_rect(endpoint.bounds),
             .note_id = endpoint.note.value,
             .lane_id = endpoint.lane.value,
             .kind = static_cast<std::uint8_t>(endpoint.kind),
             .tail = endpoint.tail,
             .selected = endpoint.selected,
             .proposal_origin = endpoint.proposal_origin,
             .proposal_ghost = endpoint.proposal_ghost,
             .semantic_color_visible = endpoint.semantic_color.has_value(),
             .semantic_red = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->red : 0),
             .semantic_green = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->green : 0),
             .semantic_blue = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->blue : 0),
             .semantic_alpha = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->alpha : 0),
             .paint_preview = endpoint.paint_preview,
             .paint_erase_preview = endpoint.paint_erase_preview,
             .paint_invalid_preview = endpoint.paint_invalid_preview});
    }
    const auto& bgm_lanes = scene.bgm_lanes;
    const auto bgm_lane_count =
        std::min(bgm_lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_LANE_CAPACITY));
    flattened.compose_bgm_lane_storage.reserve(bgm_lane_count);
    for (std::size_t index = 0; index < bgm_lane_count; ++index) {
        const auto& lane = bgm_lanes[index];
        flattened.compose_bgm_lane_storage.push_back({.bounds = convert_rect(lane.bounds),
                                                      .source_lane_id = lane.source_lane.value,
                                                      .source_order = lane.source_order});
    }
    const auto& bgm_events = scene.bgm_events;
    const auto bgm_event_count =
        std::min(bgm_events.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    flattened.compose_bgm_event_storage.reserve(bgm_event_count);
    for (std::size_t index = 0; index < bgm_event_count; ++index) {
        const auto& event = bgm_events[index];
        flattened.compose_bgm_event_storage.push_back(
            {.bounds = convert_rect(event.bounds),
             .event_id = event.event.value,
             .source_lane_id = event.source_lane.value,
             .sound_id = event.sound ? event.sound->value : 0,
             .has_sound = event.sound.has_value(),
             .selected = event.selected});
    }
    if (scene.cursor) {
        flattened.compose_cursor_storage = WormUiComposeCursorView{
            .bounds = convert_rect(scene.cursor->bounds),
            .lane_id = scene.cursor->lane.value,
        };
    }
    view.truncated = lane_key_bindings_truncated || band_count != bands.size() ||
                     lane_count != lanes.size() || line_count != lines.size() ||
                     difference_count != differences.size() || hold_count != holds.size() ||
                     endpoint_count != endpoints.size() || bgm_lane_count != bgm_lanes.size() ||
                     bgm_event_count != bgm_events.size();
    view.bands = flattened.compose_band_storage.data();
    view.band_count = flattened.compose_band_storage.size();
    view.lanes = flattened.compose_lane_storage.data();
    view.lane_count = flattened.compose_lane_storage.size();
    view.grid_lines = flattened.compose_grid_storage.data();
    view.grid_line_count = flattened.compose_grid_storage.size();
    view.differences = flattened.compose_difference_storage.data();
    view.difference_count = flattened.compose_difference_storage.size();
    view.hold_bodies = flattened.compose_hold_storage.data();
    view.hold_body_count = flattened.compose_hold_storage.size();
    view.note_endpoints = flattened.compose_note_storage.data();
    view.note_endpoint_count = flattened.compose_note_storage.size();
    view.bgm_lanes = flattened.compose_bgm_lane_storage.data();
    view.bgm_lane_count = flattened.compose_bgm_lane_storage.size();
    view.bgm_events = flattened.compose_bgm_event_storage.data();
    view.bgm_event_count = flattened.compose_bgm_event_storage.size();
    view.cursor = flattened.compose_cursor_storage ? &*flattened.compose_cursor_storage : nullptr;
}

auto first_recovery_diagnostic(
    const wormedit::native_project_recovery_controller_dispatch_result_t& result) -> std::string {
    if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    if (result.action_result && !result.action_result->diagnostics.empty()) {
        const auto& diagnostic = result.action_result->diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    return {};
}

auto autosave_status_text(const wormedit::native_project_autosave_result_t& result) -> std::string {
    using status_t = wormedit::native_project_autosave_status_t;
    if (result.status == status_t::published)
        return "Autosaved revision " + std::to_string(result.revision) + ".";
    if (result.status != status_t::failed)
        return {};
    if (result.diagnostics.empty())
        return "Autosave failed; the previous recovery backup was retained.";
    const auto& diagnostic = result.diagnostics.front();
    auto message = "Autosave failed: " + diagnostic.message;
    if (!diagnostic.path.empty())
        message += " (" + diagnostic.path.generic_string() + ")";
    return message;
}

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version,
    wormedit::native_project_autosave_loop_t& autosave_loop) -> std::string {
    using namespace wormedit;
    const native_project_recovery_action_target_t target{
        .scan_generation = action.recovery_scan_generation,
        .row_index = action.recovery_row_index,
        .candidate_index = action.recovery_candidate_index,
    };
    native_project_recovery_controller_action_t request =
        inspect_native_project_recovery_action_t{.target = target};
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        request = inspect_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!history.undo.empty() || !history.redo.empty()) {
            return "The current workspace changed after recovery discovery; save or restart "
                   "before replacing it with a recovery candidate.";
        }
        request = recover_native_project_recovery_action_t{
            .target = target,
            .expected_session =
                {
                    .is_set = true,
                    .version =
                        scanned_primary_version.has_value()
                            ? std::optional<
                                  native_project_document_version_t>{native_project_document_version_t{
                                  .project_id = history.document.id,
                                  .revision = history.document.revision}}
                            : std::nullopt,
                },
        };
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS: {
        const auto end = std::find(std::begin(action.recovery_save_as_destination),
                                   std::end(action.recovery_save_as_destination), '\0');
        const std::string destination(action.recovery_save_as_destination, end);
        if (destination.empty()) {
            return "Choose a new project path before using Save as.";
        }
        request = save_as_native_project_recovery_action_t{
            .target = target, .destination = std::filesystem::path{destination}};
        break;
    }
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        request = discard_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        request = quarantine_native_project_recovery_action_t{.target = target};
        break;
    default:
        return "Invalid recovery action.";
    }

    const auto dispatched = controller.dispatch(request);
    const auto diagnostic = first_recovery_diagnostic(dispatched);
    if (!dispatched.accepted) {
        return diagnostic.empty() ? "Recovery action was rejected." : diagnostic;
    }
    if (!dispatched.action_result) {
        return diagnostic.empty() ? "Recovery action did not produce a result." : diagnostic;
    }
    const auto& result = *dispatched.action_result;
    if (result.has_errors() || !result.applied) {
        return diagnostic.empty() ? "Recovery action did not complete." : diagnostic;
    }

    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        if (result.document) {
            return "Inspected recovery candidate read-only: " + result.document->metadata.title +
                   " (revision " + std::to_string(result.document->revision) + ").";
        }
        return "Recovery candidate inspected read-only.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!result.document) {
            return "Recovery action completed without a replacement document.";
        }
        {
            auto replacement_history = make_document_history(*result.document, 128, false);
            auto replacement_binding = make_workspace_controller(replacement_history);
            if (!replacement_binding) {
                return "Recovered data validated, but the workspace could not be rebound; "
                       "the current workspace was kept.";
            }
            const auto& recovery_view = controller.view();
            std::optional<native_project_document_version_t> recovered_save_baseline;
            auto recovered_sequence = std::uint64_t{};
            if (action.recovery_row_index < recovery_view.rows.size()) {
                const auto& row = recovery_view.rows[action.recovery_row_index];
                if (row.last_explicit_save_revision) {
                    recovered_save_baseline = native_project_document_version_t{
                        .project_id = result.document->id,
                        .revision = *row.last_explicit_save_revision};
                }
                if (row.recovery_session_sequence)
                    recovered_sequence = *row.recovery_session_sequence;
            }
            if (!autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                             recovered_save_baseline, recovered_sequence)) {
                return "Wait for the current autosave to finish before recovering a session.";
            }
            history = std::move(replacement_history);
            workspace = std::move(*replacement_binding.workspace);
        }
        // A recovered document is no longer the primary baseline captured by
        // this scan. Invalidate every remaining row token before returning to
        // the event loop so another click cannot replace the recovered session.
        (void)controller.replace_scan({});
        return "Recovery loaded into the current session. Review it and save explicitly.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS:
        return "Recovery candidate saved as a new project; the current workspace was unchanged.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return "Recovery artifact moved to its recoverable discarded location.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return result.quarantined_path
                   ? "Recovery artifact quarantined at " + result.quarantined_path->generic_string()
                   : "Recovery artifact quarantined.";
    default:
        return "Invalid recovery action.";
    }
}

auto dispatch_metadata_ui_action(const WormUiWorkspaceAction& action,
                                 wormedit::document_history_t& history,
                                 wormedit::workspace_controller_t& workspace, bool timing_locked)
    -> std::string {
    using namespace wormedit;
    const auto failed = [](const document_command_result_t& result) {
        if (!result.diagnostics.empty())
            return result.diagnostics.front().message;
        return std::string{"Metadata edit was not applied."};
    };
    const auto publish = [&](document_command_result_t result) {
        if (!result.applied)
            return result.diagnostics.empty() ? std::string{"No metadata changes."}
                                              : failed(result);
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? std::string{"Metadata changed, but workspace rebinding failed."}
                       : rebound.diagnostics.front().message;
        }
        return std::string{"Metadata updated."};
    };

    switch (action.metadata_edit) {
    case WORM_UI_METADATA_EDIT_SONG: {
        if (!action.metadata_title || !action.metadata_artist ||
            action.metadata_search_tag_count > WORM_UI_METADATA_SEARCH_TAG_CAPACITY ||
            (action.metadata_search_tag_count != 0 && !action.metadata_search_tags)) {
            return "Metadata edit payload is incomplete or exceeds the tag-count cap.";
        }
        std::vector<std::string> tags;
        tags.reserve(action.metadata_search_tag_count);
        for (std::size_t index = 0; index < action.metadata_search_tag_count; ++index) {
            if (!action.metadata_search_tags[index])
                return "Metadata edit contains an empty tag pointer.";
            tags.emplace_back(action.metadata_search_tags[index]);
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision,
                                           set_song_metadata_action_t{action.metadata_title,
                                                                      action.metadata_artist,
                                                                      std::move(tags)}));
    }
    case WORM_UI_METADATA_EDIT_CHART: {
        if (action.metadata_chart_id == 0 || !action.metadata_chart_name ||
            !action.metadata_chart_author ||
            (action.metadata_has_chart_rating &&
             (!action.metadata_chart_rating_numerator_text ||
              !action.metadata_chart_rating_denominator_text))) {
            return "Chart metadata edit payload is incomplete.";
        }
        const auto rating = make_chart_rating_metadata_edit(
            chart_id_t{action.metadata_chart_id}, !action.metadata_has_chart_rating,
            action.metadata_chart_rating_numerator_text
                ? action.metadata_chart_rating_numerator_text
                : "",
            action.metadata_chart_rating_denominator_text
                ? action.metadata_chart_rating_denominator_text
                : "");
        if (!rating) {
            return "Rating must use a signed 64-bit numerator and positive 64-bit denominator.";
        }
        return publish(apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{chart_id_t{action.metadata_chart_id},
                                        action.metadata_chart_name, action.metadata_chart_author,
                                        rating->rating}));
    }
    case WORM_UI_METADATA_EDIT_AUDIO_OFFSET: {
        if (action.metadata_chart_id == 0 || !action.metadata_audio_offset_numerator_text ||
            !action.metadata_audio_offset_denominator_text) {
            return "Audio offset edit payload is incomplete.";
        }
        const auto numerator = parse_timing_signed(action.metadata_audio_offset_numerator_text);
        const auto denominator =
            parse_timing_unsigned(action.metadata_audio_offset_denominator_text);
        if (!numerator || !denominator || *denominator == 0) {
            return "Audio offset must use a signed 64-bit numerator and positive 64-bit "
                   "denominator.";
        }
        return publish(apply_audio_offset_edit(history, action.metadata_document_revision,
                                               chart_id_t{action.metadata_chart_id},
                                               {*numerator, *denominator}, timing_locked));
    }
    case WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD: {
        if (action.metadata_format_family > static_cast<std::uint8_t>(format_family_t::quaver)) {
            return "Format metadata edit refers to an unknown format family.";
        }
        const format_metadata_target_t target{
            action.metadata_format_chart_scoped
                ? std::optional<chart_id_t>{chart_id_t{action.metadata_chart_id}}
                : std::nullopt,
            static_cast<format_family_t>(action.metadata_format_family)};
        metadata_edit_action_t edit;
        if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata replacement payload is incomplete.";
            edit = replace_format_metadata_field_action_t{target, action.metadata_format_index,
                                                          action.metadata_format_name,
                                                          action.metadata_format_value};
        } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
            edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
        } else {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata insertion payload is incomplete.";
            edit = insert_format_metadata_field_action_t{target, action.metadata_format_index,
                                                         action.metadata_format_name,
                                                         action.metadata_format_value};
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision, edit));
    }
    case WORM_UI_METADATA_EDIT_NONE:
        break;
    }
    return "Invalid metadata edit action.";
}

auto dispatch_audio_offset_drag_action(const WormUiWorkspaceAction& action,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       bool timing_locked,
                                       std::optional<std::uint32_t> selected_sample_rate,
                                       std::optional<wormedit::audio_offset_drag_t>& drag,
                                       std::optional<wormedit::workspace_tab_t>& drag_tab)
    -> std::string {
    using namespace wormedit;
    const auto supported_tab = workspace.active_tab == workspace_tab_t::audio_slices ||
                               workspace.active_tab == workspace_tab_t::metadata;
    const auto context_matches =
        supported_tab && workspace.selected_chart &&
        workspace.selected_chart->value == action.audio_offset_drag_chart_id &&
        history.document.revision == action.audio_offset_drag_document_revision &&
        selected_sample_rate && *selected_sample_rate == action.audio_offset_drag_sample_rate;
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag cancelled.";
    }
    if (!context_matches) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag expired after its chart or waveform changed.";
    }
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN) {
        const auto begun = begin_audio_offset_drag(history.document, *workspace.selected_chart,
                                                   *selected_sample_rate, timing_locked);
        drag = begun.drag;
        drag_tab = begun ? std::optional{workspace.active_tab} : std::nullopt;
        return begun                       ? "Dragging waveform offset; release to apply."
               : begun.diagnostics.empty() ? "Audio offset drag could not start."
                                           : begun.diagnostics.front().message;
    }
    if (!drag || !drag_tab || *drag_tab != workspace.active_tab ||
        drag->chart != *workspace.selected_chart ||
        drag->document_revision != action.audio_offset_drag_document_revision ||
        drag->sample_rate != *selected_sample_rate) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag has no matching captured transaction.";
    }
    const auto updated = update_audio_offset_drag(
        history.document, *drag, action.audio_offset_drag_delta_frames, timing_locked);
    if (!updated) {
        drag.reset();
        drag_tab.reset();
        return updated.diagnostics.empty() ? "Audio offset preview was rejected."
                                           : updated.diagnostics.front().message;
    }
    drag = updated.drag;
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_UPDATE)
        return "Previewing exact waveform offset.";
    if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT) {
        drag.reset();
        drag_tab.reset();
        return "Invalid audio offset drag action.";
    }
    const auto committed = commit_audio_offset_drag(history, *drag, timing_locked);
    drag.reset();
    drag_tab.reset();
    if (!committed.applied) {
        return committed.diagnostics.empty() ? "Audio offset was unchanged."
                                             : committed.diagnostics.front().message;
    }
    const auto rebound = rebind_workspace(workspace, history);
    return rebound                       ? "Audio offset updated from waveform drag."
           : rebound.diagnostics.empty() ? "Audio offset changed, but workspace rebinding failed."
                                         : rebound.diagnostics.front().message;
}

auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string {
    if (result.accepted) {
        return std::string(accepted);
    }
    return result.diagnostics.empty() ? "Compose action was rejected."
                                      : result.diagnostics.front().message;
}

auto compose_view_for_state(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t> {
    const auto setup = wormedit::make_editor_interaction(
        history, state.chart, state.active_lane, state.cursor, state.supported_snap_denominators,
        state.active_snap_denominator, state.viewport_height_pixels);
    if (!setup.view) {
        return std::nullopt;
    }
    auto view = *setup.view;
    view.pixels_per_standard_measure = state.pixels_per_standard_measure;
    view.viewport_scroll_pixels = state.viewport_scroll_pixels;
    view.pending_hold = state.pending_hold;
    view.selected_notes = state.selected_notes;
    view.selected_objects = state.selected_objects;
    return view;
}

auto compose_navigation_action(WormUiComposeNavigationAction action)
    -> std::optional<wormedit::editor_navigation_action_t> {
    using namespace wormedit;
    switch (action) {
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD:
        return editor_navigation_action_t::cursor_backward;
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD:
        return editor_navigation_action_t::cursor_forward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD:
        return editor_navigation_action_t::page_backward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD:
        return editor_navigation_action_t::page_forward;
    }
    return std::nullopt;
}

auto dispatch_compose_navigation_actions(const wormedit::editor_navigation_action_batch_t& actions,
                                         wormedit::document_history_t& history,
                                         wormedit::editor_interaction_state_t& state)
    -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < actions.count; ++index) {
        editor_interaction_result_t result;
        std::string_view accepted;
        switch (actions[index]) {
        case editor_navigation_action_t::cursor_backward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
            accepted = "Edit cursor moved backward.";
            break;
        case editor_navigation_action_t::cursor_forward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
            accepted = "Edit cursor moved forward.";
            break;
        case editor_navigation_action_t::page_backward:
            result = dispatch_editor_action(
                history, state, page_edit_cursor_action_t{interaction_direction_t::backward, 1});
            accepted = "Edit cursor paged backward by one measure.";
            break;
        case editor_navigation_action_t::page_forward:
            result = dispatch_editor_action(
                history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 1});
            accepted = "Edit cursor paged forward by one measure.";
            break;
        }
        status = compose_result_status(result, accepted);
        if (!result.accepted)
            break;
    }
    return status;
}

auto dispatch_compose_input_action(const WormUiWorkspaceAction& ui_action,
                                   wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::editor_interaction_state_t& state,
                                   wormedit::viewport_input_controller_t& viewport_input,
                                   wormedit::editor_navigation_input_controller_t& navigation_input,
                                   application_command_state_t& application_state,
                                   compose_scene_cache_t& scene_cache,
                                   double viewport_delta_seconds) -> std::string {
    using namespace wormedit;
    scene_cache.audition_request.reset();
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != ui_action.compose_input_chart_id ||
        state.chart.value != ui_action.compose_input_chart_id ||
        history.document.revision != ui_action.compose_input_document_revision ||
        !std::isfinite(ui_action.compose_input_viewport_width_pixels) ||
        ui_action.compose_input_viewport_width_pixels <= 0.0) {
        return "Compose input ignored because the chart or document revision changed.";
    }
    if (ui_action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose input batch exceeded its capacity; the active gesture was canceled.";
    }
    if (ui_action.compose_viewport_input_count > WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose viewport input exceeded its capacity; the active gesture was canceled.";
    }
    const auto input_chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto bgm_lane_count = input_chart == history.document.charts.end()
                                    ? std::size_t{}
                                    : input_chart->bgm_source_lanes.size();
    const auto bgm_pane_width =
        compose_bgm_pane_width(ui_action.compose_input_viewport_width_pixels, bgm_lane_count != 0);
    const auto field_left_pixels = compose_measure_rail_width_pixels;
    const auto field_width_pixels =
        compose_playable_field_width(ui_action.compose_input_viewport_width_pixels, bgm_pane_width);

    if (!ui_action.shortcut_window_focused) {
        viewport_input.focus_lost();
        scene_cache.viewport_autoscroll_active = false;
    } else {
        if (ui_action.compose_viewport_middle_click) {
            const auto middle_click_pane =
                bgm_pane_width > 0.0 &&
                        std::isfinite(ui_action.compose_viewport_middle_click_x_pixels) &&
                        ui_action.compose_viewport_middle_click_x_pixels >=
                            field_left_pixels + field_width_pixels &&
                        ui_action.compose_viewport_middle_click_x_pixels <=
                            ui_action.compose_input_viewport_width_pixels
                    ? viewport_pane_t::bgm
                    : viewport_pane_t::chart;
            viewport_input.middle_click(middle_click_pane,
                                        ui_action.compose_viewport_middle_click_x_pixels,
                                        ui_action.compose_viewport_middle_click_y_pixels);
            scene_cache.viewport_autoscroll_active = viewport_input.autoscroll_active();
            if (scene_cache.viewport_autoscroll_active) {
                scene_cache.viewport_autoscroll_anchor_x_pixels =
                    ui_action.compose_viewport_middle_click_x_pixels;
                scene_cache.viewport_autoscroll_anchor_y_pixels =
                    ui_action.compose_viewport_middle_click_y_pixels;
            }
        }
        if (ui_action.compose_viewport_pointer_valid) {
            viewport_input.update_autoscroll_pointer(ui_action.compose_viewport_pointer_x_pixels,
                                                     ui_action.compose_viewport_pointer_y_pixels);
            scene_cache.viewport_autoscroll_pointer_x_pixels =
                ui_action.compose_viewport_pointer_x_pixels;
            scene_cache.viewport_autoscroll_pointer_y_pixels =
                ui_action.compose_viewport_pointer_y_pixels;
        }
    }
    if (ui_action.compose_input_count != 0 && !ui_action.compose_viewport_middle_click) {
        if (viewport_input.normal_edit_press()) {
            scene_cache.viewport_autoscroll_active = false;
        }
    }

    auto status = std::string{};
    const auto selection_mode = [](WormUiComposeSelectionMode mode)
        -> std::optional<wormedit::editor_area_selection_mode_t> {
        switch (mode) {
        case WORM_UI_COMPOSE_SELECTION_REPLACE:
            return wormedit::editor_area_selection_mode_t::replace;
        case WORM_UI_COMPOSE_SELECTION_ADD:
            return wormedit::editor_area_selection_mode_t::add;
        case WORM_UI_COMPOSE_SELECTION_TOGGLE:
            return wormedit::editor_area_selection_mode_t::toggle;
        }
        return std::nullopt;
    };
    const auto hit_target_at = [&](const chart_document_t& chart,
                                   const editor_interaction_view_model_t& view, double x_pixels,
                                   double y_pixels) -> std::optional<editor_hit_target_t> {
        if (!scene_cache.hit_test_index) {
            auto built = build_editor_hit_test_index(chart, history.document.revision);
            if (!std::holds_alternative<editor_hit_test_index_t>(built)) {
                return std::nullopt;
            }
            scene_cache.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
        }
        return hit_test_editor(chart, *scene_cache.hit_test_index, view,
                               {.x_pixels = x_pixels,
                                .y_pixels = y_pixels,
                                .field_left_pixels = field_left_pixels,
                                .field_width_pixels = field_width_pixels});
    };
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    const auto gesture_active = state.pending_hold.has_value() ||
                                state.pending_note_duration_gesture.has_value() ||
                                state.pending_note_move_gesture.has_value() ||
                                state.pending_note_paint_gesture.has_value() ||
                                state.pending_semantic_layer_paint_gesture.has_value() ||
                                state.pending_editor_area_selection.has_value() ||
                                state.pending_space_range_selection.has_value();
    if (!ui_action.compose_has_pointer_hover || gesture_active ||
        chart == history.document.charts.end() || !scene_cache.interaction_view) {
        scene_cache.hover.reset();
    } else {
        if (!scene_cache.hit_test_index) {
            auto built = build_editor_hit_test_index(*chart, history.document.revision);
            if (std::holds_alternative<editor_hit_test_index_t>(built)) {
                scene_cache.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
            }
        }
        if (scene_cache.hit_test_index) {
            scene_cache.hover = hover_editor_object(
                *chart, *scene_cache.hit_test_index, *scene_cache.interaction_view,
                {.x_pixels = ui_action.compose_pointer_hover_x_pixels,
                 .y_pixels = ui_action.compose_pointer_hover_y_pixels,
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
        }
    }
    for (std::size_t index = 0; index < ui_action.compose_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose input was discarded after the document revision changed.";
        }

        const auto& input = ui_action.compose_inputs[index];
        editor_interaction_result_t result;
        std::string_view accepted = "Compose interaction applied.";
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS: {
            const auto navigation = compose_navigation_action(input.navigation_action);
            if (!navigation) {
                return "Compose navigation input used an unknown action.";
            }
            status = dispatch_compose_navigation_actions(
                navigation_input.press(*navigation, static_cast<std::uint64_t>(SDL_GetTicks())),
                history, state);
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE: {
            const auto navigation = compose_navigation_action(input.navigation_action);
            if (!navigation) {
                return "Compose navigation release used an unknown action.";
            }
            navigation_input.release(*navigation);
            status = "Compose navigation key released.";
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL:
            navigation_input.cancel();
            status = "Compose navigation canceled.";
            continue;
        case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
            accepted = "Edit cursor moved backward.";
            break;
        case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
            accepted = "Edit cursor moved forward.";
            break;
        case WORM_UI_COMPOSE_INPUT_DELETE_NOTES:
            result = dispatch_editor_action(history, state, delete_editor_notes_action_t{});
            accepted = result.document_changed ? "Note deleted." : "No note at the cursor.";
            break;
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
            if (input.lane_id == 0) {
                return "Lane-key input did not identify a chart lane.";
            }
            if (!state.selected_notes.empty()) {
                result = dispatch_editor_action(history, state,
                                                assign_selected_notes_to_lane_action_t{
                                                    lane_id_t{input.lane_id}, input.successive});
                accepted = input.successive ? "Next selected note assigned to lane."
                                            : "Selected notes assigned to lane.";
            } else {
                result = dispatch_editor_action(
                    history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
                accepted = "Lane hold started; move the edit cursor and release the key to finish.";
            }
            break;
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE:
            if (state.pending_hold.has_value() &&
                state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) {
                result = dispatch_editor_action(history, state,
                                                release_lane_key_hold_gesture_action_t{});
                accepted = result.document_changed ? "Lane hold committed." : "Lane hold released.";
            } else {
                result.accepted = true;
                accepted = "Lane key released.";
            }
            break;
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                return "Alt/Option-drag must begin over an editable lane.";
            }
            if (target->note && target->note_kind &&
                (*target->note_kind == note_kind_t::tap ||
                 *target->note_kind == note_kind_t::hold)) {
                state.active_lane = target->lane;
                state.cursor = target->position;
                result = dispatch_editor_action(
                    history, state, begin_note_duration_gesture_action_t{*target->note});
                accepted = "Note resize started; drag to change duration and release to finish.";
            } else if (target->note) {
                return "Alt/Option-drag resizes tap and hold notes; this note kind is not "
                       "resizable.";
            } else {
                result = dispatch_editor_action(history, state,
                                                begin_alt_pointer_hold_gesture_action_t{
                                                    .lane = target->lane,
                                                    .position = target->position,
                                                    .alt_or_option_down = true,
                                                });
                accepted = "Pointer hold started; release to finish.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto y = std::clamp(input.y_pixels, 0.0,
                                      std::max(0.0, current_view->viewport_height_pixels - 0.001));
            const auto position = editor_position_at_y_pixels(*chart, *current_view, y);
            if (!position) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "Pointer hold input fell outside chart geometry and was canceled.";
            }
            if (state.pending_note_duration_gesture) {
                if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                    result = dispatch_editor_action(
                        history, state, update_note_duration_gesture_action_t{*position});
                    accepted = "Note resize preview updated.";
                } else {
                    result = dispatch_editor_action(
                        history, state, release_note_duration_gesture_action_t{*position});
                    accepted = result.document_changed ? "Note duration committed."
                                                       : "Note resize released.";
                }
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                result = dispatch_editor_action(history, state,
                                                update_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted = "Pointer hold preview updated.";
            } else {
                result = dispatch_editor_action(history, state,
                                                release_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted =
                    result.document_changed ? "Pointer hold committed." : "Pointer hold released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE:
        case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE: {
            if (!application_state.semantic_layer_paint_mode_active ||
                !application_state.semantic_layer_target) {
                if (state.pending_semantic_layer_paint_gesture) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted =
                        "Semantic-layer paint stroke canceled because paint mode is inactive.";
                } else {
                    result.accepted = true;
                    accepted = "Semantic-layer paint input ignored because paint mode is inactive.";
                }
                break;
            }
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto* layer =
                visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
            if (!layer || layer->locked) {
                application_state.semantic_layer_paint_mode_active = false;
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "Choose an unlocked visible semantic layer before painting note membership.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target || !target->note) {
                if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
                    return "Begin semantic-layer painting over a playable note.";
                }
                if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
                    result.accepted = true;
                    accepted = "Semantic-layer paint moved between playable notes.";
                    break;
                }
                result =
                    dispatch_editor_action(history, state,
                                           end_semantic_layer_paint_gesture_action_t{
                                               .note = object_id_t{}, .pointer_captured = true});
                accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                                   : "Semantic-layer paint stroke released.";
                break;
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
                result = dispatch_editor_action(
                    history, state,
                    begin_semantic_layer_paint_gesture_action_t{
                        .layer = layer->id, .note = *target->note, .pointer_captured = true});
                accepted = "Semantic-layer paint stroke started.";
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE) {
                result =
                    dispatch_editor_action(history, state,
                                           update_semantic_layer_paint_gesture_action_t{
                                               .note = *target->note, .pointer_captured = true});
                accepted = "Semantic-layer paint preview updated.";
            } else {
                result =
                    dispatch_editor_action(history, state,
                                           end_semantic_layer_paint_gesture_action_t{
                                               .note = *target->note, .pointer_captured = true});
                accepted = result.document_changed ? "Semantic-layer paint stroke committed."
                                                   : "Semantic-layer paint stroke released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
        case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE: {
            if (!application_state.note_paint_mode_active) {
                if (state.pending_note_paint_gesture) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted = "Note-paint stroke canceled because paint mode is inactive.";
                } else {
                    result.accepted = true;
                    accepted = "Note-paint input ignored because paint mode is inactive.";
                }
                break;
            }
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                if (input.kind != WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
                    result =
                        dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    accepted = "Note-paint stroke canceled outside editable chart geometry.";
                    break;
                }
                return "Begin note painting over an editable chart lane.";
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
                result = dispatch_editor_action(
                    history, state,
                    begin_note_paint_gesture_action_t{.lane = target->lane,
                                                      .position = target->position,
                                                      .pointer_captured = true});
                accepted = "Note-paint stroke started.";
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE) {
                result = dispatch_editor_action(
                    history, state,
                    update_note_paint_gesture_action_t{.lane = target->lane,
                                                       .position = target->position,
                                                       .pointer_captured = true});
                accepted = "Note-paint preview updated.";
            } else {
                result = dispatch_editor_action(
                    history, state,
                    end_note_paint_gesture_action_t{.lane = target->lane,
                                                    .position = target->position,
                                                    .pointer_captured = true});
                accepted = result.document_changed ? "Note-paint stroke committed."
                                                   : "Note-paint stroke released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN:
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE:
        case WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN) {
                const auto target =
                    hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
                if (!target || !target->note ||
                    std::ranges::find(state.selected_notes, *target->note) ==
                        state.selected_notes.end()) {
                    return "Begin a note move by dragging a selected playable note.";
                }
                state.active_lane = target->lane;
                state.cursor = target->position;
                result = dispatch_editor_action(history, state,
                                                begin_note_move_gesture_action_t{*target->note});
                accepted = "Selected-note move started.";
            } else {
                const auto y =
                    std::clamp(input.y_pixels, 0.0,
                               std::max(0.0, current_view->viewport_height_pixels - 0.001));
                const auto position = editor_position_at_y_pixels(*chart, *current_view, y);
                const auto target =
                    hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
                if (!position || !target) {
                    (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                    return "Note move input fell outside chart lane geometry and was canceled.";
                }
                if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE) {
                    result = dispatch_editor_action(
                        history, state, update_note_move_gesture_action_t{*position, target->lane});
                    accepted = "Selected-note move preview updated.";
                } else {
                    result = dispatch_editor_action(
                        history, state,
                        release_note_move_gesture_action_t{*position, target->lane});
                    accepted = result.document_changed ? "Selected notes moved."
                                                       : "Selected-note move released.";
                }
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose area selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            begin_editor_area_selection_action_t{
                                                .x_pixels = input.x_pixels,
                                                .y_pixels = input.y_pixels,
                                                .field_left_pixels = field_left_pixels,
                                                .field_width_pixels = field_width_pixels,
                                                .mode = *selection_mode(input.selection_mode),
                                            });
            accepted = "Area selection started.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE:
            result = dispatch_editor_action(
                history, state,
                update_editor_area_selection_action_t{input.x_pixels, input.y_pixels, true});
            accepted = "Area selection preview updated.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE:
            result = dispatch_editor_action(
                history, state,
                commit_editor_area_selection_action_t{input.x_pixels, input.y_pixels, true});
            accepted = "Area selection committed.";
            break;
        case WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose Space selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            space_range_selection_action_t{
                                                .mode = *selection_mode(input.selection_mode),
                                            });
            accepted = state.pending_space_range_selection
                           ? "Space range selection started; move the cursor and press Space again."
                           : "Space range selection committed.";
            break;
        case WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            if (!scene_cache.hit_test_index) {
                auto built = build_editor_hit_test_index(*chart, history.document.revision);
                if (std::holds_alternative<editor_hit_test_index_t>(built)) {
                    scene_cache.hit_test_index =
                        std::get<editor_hit_test_index_t>(std::move(built));
                }
            }
            if (!scene_cache.hit_test_index || !scene_cache.interaction_view) {
                return "The chart field hover index is unavailable.";
            }
            const auto hover = hover_editor_object(
                *chart, *scene_cache.hit_test_index, *scene_cache.interaction_view,
                {.x_pixels = input.x_pixels,
                 .y_pixels = input.y_pixels,
                 .field_left_pixels = field_left_pixels,
                 .field_width_pixels = field_width_pixels,
                 .hit_radius_pixels = 8.0,
                 .scope = {.notes = true, .bgm = false, .timing = false, .bga = false}});
            if (!hover || hover->object.kind != editor_selection_object_kind_t::note) {
                return "Double-click a note to edit its label.";
            }
            result = dispatch_editor_action(history, state,
                                            select_editor_notes_action_t{{hover->object.id}});
            if (!result) {
                return compose_result_status(result, "Note label editor could not be focused.");
            }
            scene_cache.focus_note_label_editor = true;
            accepted = "Note label editor focused.";
            break;
        }
        case WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK:
            if (input.object_id == 0) {
                return "BGM event click did not identify an event.";
            }
            if (!selection_mode(input.selection_mode).has_value()) {
                return "BGM event click used an unknown selection mode.";
            }
            result = dispatch_editor_action(
                history, state,
                select_editor_objects_action_t{
                    {{editor_selection_object_kind_t::bgm_event, object_id_t{input.object_id}}},
                    *selection_mode(input.selection_mode),
                    true});
            accepted = "BGM event selected.";
            break;
        case WORM_UI_COMPOSE_INPUT_POINTER_CLICK: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto target =
                hit_target_at(*chart, *current_view, input.x_pixels, input.y_pixels);
            if (!target) {
                return "Click inside an editable lane to place or select a note.";
            }
            state.active_lane = target->lane;
            state.cursor = target->position;
            if (target->note && target->note_kind != note_kind_t::tap) {
                result = dispatch_editor_action(
                    history, state,
                    select_editor_objects_action_t{
                        {{editor_selection_object_kind_t::note, *target->note}},
                        editor_area_selection_mode_t::replace,
                        true});
                accepted = "Note selected.";
            } else {
                result =
                    dispatch_editor_action(history, state, place_tap_action_t{target->position});
                accepted =
                    result.document_changed ? "Tap note toggled." : "Tap note was not changed.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_UNDO:
        case WORM_UI_COMPOSE_INPUT_REDO: {
            (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(history, state, select_editor_notes_action_t{});
            const bool changed = input.kind == WORM_UI_COMPOSE_INPUT_UNDO
                                     ? undo_document_command(history)
                                     : redo_document_command(history);
            status = changed ? (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Undo applied."
                                                                         : "Redo applied.")
                             : (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Nothing to undo."
                                                                         : "Nothing to redo.");
            if (changed) {
                const auto rebound = rebind_workspace(workspace, history);
                if (!rebound) {
                    return rebound.diagnostics.empty()
                               ? "History changed, but workspace rebinding failed."
                               : rebound.diagnostics.front().message;
                }
                return status;
            }
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE:
            navigation_input.cancel();
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Compose gesture canceled.";
            break;
        case WORM_UI_COMPOSE_INPUT_NONE:
        default:
            continue;
        }
        status = compose_result_status(result, accepted);
        if (result.accepted && result.audition.has_value()) {
            // Last accepted sound click in the bounded input batch wins. The
            // runtime audio layer consumes this revision-bound request once it
            // owns decoded source bindings and an SDL output stream.
            scene_cache.audition_request = result.audition;
        }
        if (result.accepted && result.document_changed) {
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound) {
                return rebound.diagnostics.empty()
                           ? "Compose edit applied, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
            }
        }
    }
    if (ui_action.compose_note_label_focus_consumed) {
        scene_cache.focus_note_label_editor = false;
    }

    const auto apply_viewport_actions = [&](const viewport_action_batch_t& actions) {
        for (std::size_t action_index = 0; action_index < actions.count; ++action_index) {
            const auto& viewport_action = actions[action_index];
            if (const auto* scroll = std::get_if<viewport_scroll_action_t>(&viewport_action)) {
                if (scroll->pane == viewport_pane_t::bgm) {
                    if (scroll->delta_x_pixels == 0.0)
                        continue;
                    const auto maximum_scroll = std::max(
                        0.0, static_cast<double>(bgm_lane_count) * compose_bgm_lane_width_pixels -
                                 bgm_pane_width);
                    auto& retained_scroll =
                        application_state.bgm_horizontal_scroll_pixels[state.chart.value];
                    retained_scroll =
                        std::clamp(retained_scroll + scroll->delta_x_pixels, 0.0, maximum_scroll);
                    status = "Compose BGM pane scrolled.";
                    continue;
                }
                if (scroll->delta_y_pixels == 0.0) {
                    continue;
                }
                const auto result = dispatch_editor_action(
                    history, state, scroll_editor_view_action_t{scroll->delta_y_pixels});
                status = compose_result_status(result, "Compose field scrolled.");
            } else if (const auto* zoom = std::get_if<viewport_zoom_action_t>(&viewport_action)) {
                const auto result = dispatch_editor_action(
                    history, state,
                    adjust_editor_zoom_action_t{.factor = zoom->factor,
                                                .anchor_screen_y_pixels = zoom->cursor_y_pixels});
                status = compose_result_status(result, "Compose field zoomed at the pointer.");
            }
        }
    };
    for (std::size_t index = 0; index < ui_action.compose_viewport_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose viewport input was discarded after the chart or document "
                   "changed.";
        }
        const auto& input = ui_action.compose_viewport_inputs[index];
        const auto input_pane =
            bgm_pane_width > 0.0 && std::isfinite(input.cursor_x_pixels) &&
                    input.cursor_x_pixels >= field_left_pixels + field_width_pixels &&
                    input.cursor_x_pixels <= ui_action.compose_input_viewport_width_pixels
                ? viewport_pane_t::bgm
                : viewport_pane_t::chart;
        apply_viewport_actions(viewport_input.handle_wheel({
            .pane = input_pane,
            .kind = input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                        ? viewport_wheel_kind_t::coarse_steps
                        : viewport_wheel_kind_t::smooth_pixels,
            .delta_x = input.delta_x,
            .delta_y = input.delta_y,
            .zoom_modifier = input.zoom_modifier,
            .cursor_x_pixels = input.cursor_x_pixels,
            .cursor_y_pixels = input.cursor_y_pixels,
        }));
    }
    if (std::isfinite(viewport_delta_seconds) && viewport_delta_seconds > 0.0 &&
        history.document.revision == ui_action.compose_input_document_revision &&
        workspace.active_tab == workspace_tab_t::compose && workspace.selected_chart &&
        workspace.selected_chart->value == ui_action.compose_input_chart_id &&
        state.chart.value == ui_action.compose_input_chart_id) {
        apply_viewport_actions(viewport_input.advance(viewport_delta_seconds));
    }
    scene_cache.viewport_autoscroll_active = viewport_input.autoscroll_active();
    if (!scene_cache.viewport_autoscroll_active) {
        scene_cache.viewport_autoscroll_anchor_x_pixels = 0.0;
        scene_cache.viewport_autoscroll_anchor_y_pixels = 0.0;
        scene_cache.viewport_autoscroll_pointer_x_pixels = 0.0;
        scene_cache.viewport_autoscroll_pointer_y_pixels = 0.0;
    }
    if (!status.empty()) {
        return status;
    }
    return ui_action.compose_input_count != 0 || ui_action.compose_viewport_input_count != 0
               ? "Compose input handled."
               : std::string{};
}

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool;

auto dispatch_compose_view_action(const WormUiWorkspaceAction& action,
                                  wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height,
                                  application_command_state_t& application_state) -> std::string {
    using namespace wormedit;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        state.chart.value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return "Compose view action ignored because the chart or document revision changed.";
    }
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ZOOM_IN:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.25, .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed in.";
        break;
    case WORM_UI_COMPOSE_ACTION_ZOOM_OUT:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.0 / 1.25,
                                        .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed out.";
        break;
    case WORM_UI_COMPOSE_ACTION_SCROLL_UP:
    case WORM_UI_COMPOSE_ACTION_SCROLL_DOWN: {
        const auto direction =
            action.compose_action == WORM_UI_COMPOSE_ACTION_SCROLL_UP ? -1.0 : 1.0;
        result = dispatch_editor_action(
            history, state,
            scroll_editor_view_action_t{direction * std::max(64.0, viewport_height * 0.75)});
        status = direction < 0.0 ? "Compose field scrolled up." : "Compose field scrolled down.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_RESET_VIEW: {
        const auto factor = default_pixels_per_standard_measure / state.pixels_per_standard_measure;
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = factor, .anchor_screen_y_pixels = std::nullopt});
        if (result) {
            result = dispatch_editor_action(
                history, state, scroll_editor_view_action_t{-state.viewport_scroll_pixels});
        }
        status = "Compose field view reset.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE:
        if (editor_interaction_has_pending_gesture(state)) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            if (!result.accepted) {
                return compose_result_status(result, "Compose gesture canceled.");
            }
        }
        application_state.note_paint_mode_active = !application_state.note_paint_mode_active;
        if (application_state.note_paint_mode_active) {
            application_state.semantic_layer_paint_mode_active = false;
        }
        status = application_state.note_paint_mode_active ? "Note-paint mode enabled."
                                                          : "Note-paint mode disabled.";
        return status;
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing note labels.";
        }
        if (state.selected_notes.empty()) {
            return "Select one or more notes before editing their label.";
        }
        const auto label = action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
                               ? std::optional<std::string>{action.compose_note_label}
                               : std::optional<std::string>{};
        const auto applied = apply_note_label_edit(
            history, action.compose_input_document_revision,
            {.chart = state.chart, .notes = state.selected_notes, .label = label});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        if (!applied.applied) {
            return label ? "Selected notes already have that label."
                         : "Selected notes already have no label.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Note label changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return label ? "Note label applied." : "Note labels cleared.";
    }
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || action.compose_semantic_layer_id == 0) {
            return "Choose a visible semantic layer from the current Compose palette.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, layer_id_t{action.compose_semantic_layer_id});
        if (!layer) {
            return "That semantic-layer palette entry is no longer visible.";
        }
        application_state.semantic_layer_target = layer->id;
        return "Semantic-layer target: " + layer->name;
    }
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before navigating semantic-layer pages.";
        }
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette =
            make_semantic_layer_palette_window(*chart, application_state.semantic_layer_target,
                                               application_state.semantic_layer_palette_page, false,
                                               WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette) {
            return "Semantic-layer palette navigation exceeded its bounded limit.";
        }
        application_state.semantic_layer_palette_page = palette->page;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE) {
            if (application_state.semantic_layer_palette_page == 0) {
                return "Semantic-layer palette is already on its first page.";
            }
            --application_state.semantic_layer_palette_page;
        } else {
            if (palette->page_count == 0 ||
                application_state.semantic_layer_palette_page + 1 >= palette->page_count) {
                return "Semantic-layer palette is already on its last page.";
            }
            ++application_state.semantic_layer_palette_page;
        }
        return "Semantic-layer palette page " +
               std::to_string(application_state.semantic_layer_palette_page + 1) + " of " +
               std::to_string(palette->page_count) + ".";
    }
    case WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before adding a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before adding a semantic layer.";
        }
        std::string name = "Layer";
        for (std::size_t suffix = 2;; ++suffix) {
            const auto duplicate = std::ranges::any_of(
                chart->semantic_layers, [&](const auto& layer) { return layer.name == name; });
            if (!duplicate) {
                break;
            }
            name = "Layer " + std::to_string(suffix);
        }
        constexpr std::array<semantic_layer_rgba_t, 6> colors{{
            {0.91, 0.35, 0.31, 1.0},
            {0.95, 0.67, 0.24, 1.0},
            {0.36, 0.74, 0.36, 1.0},
            {0.25, 0.62, 0.91, 1.0},
            {0.58, 0.42, 0.84, 1.0},
            {0.87, 0.38, 0.68, 1.0},
        }};
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            insert_semantic_layer_action_t{state.chart, std::move(name),
                                           colors[chart->semantic_layers.size() % colors.size()]});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic layer was not added."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer added."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer added, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before renaming a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before renaming it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                .chart = state.chart,
                .layer = layer->id,
                .name = action.compose_semantic_layer_name,
                .color = {.red = static_cast<double>(layer->color.red) / 255.0,
                          .green = static_cast<double>(layer->color.green) / 255.0,
                          .blue = static_cast<double>(layer->color.blue) / 255.0,
                          .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                .visible = layer->visible,
                .locked = layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer name was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer renamed."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer renamed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before recoloring a semantic "
                   "layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before recoloring it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto color = semantic_layer_rgba_t{
            .red = static_cast<double>(action.compose_semantic_layer_red) / 255.0,
            .green = static_cast<double>(action.compose_semantic_layer_green) / 255.0,
            .blue = static_cast<double>(action.compose_semantic_layer_blue) / 255.0,
            .alpha = static_cast<double>(action.compose_semantic_layer_alpha) / 255.0,
        };
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{state.chart, layer->id, layer->name, color,
                                           layer->visible, layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer color was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic-layer color changed."
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer color changed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before deleting a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before deleting it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto layer_id = layer->id;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            delete_semantic_layer_action_t{
                state.chart, layer_id,
                action.compose_action ==
                    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic layer was not deleted."
                                               : applied.diagnostics.front().message;
        }
        application_state.required_semantic_layer_filters.erase(
            std::remove(application_state.required_semantic_layer_filters.begin(),
                        application_state.required_semantic_layer_filters.end(), layer_id),
            application_state.required_semantic_layer_filters.end());
        application_state.excluded_semantic_layer_filters.erase(
            std::remove(application_state.excluded_semantic_layer_filters.begin(),
                        application_state.excluded_semantic_layer_filters.end(), layer_id),
            application_state.excluded_semantic_layer_filters.end());
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer deleted."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer deleted, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before reordering semantic layers.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before reordering it.";
        }
        const auto found =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        if (found == chart->semantic_layers.end()) {
            return "The selected semantic layer is no longer available.";
        }
        const auto index =
            static_cast<std::size_t>(std::distance(chart->semantic_layers.begin(), found));
        const auto moving_up =
            action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
        if ((moving_up && index == 0) ||
            (!moving_up && index + 1 == chart->semantic_layers.size())) {
            return "Semantic layer is already at that palette boundary.";
        }
        std::vector<layer_id_t> ordered;
        ordered.reserve(chart->semantic_layers.size());
        for (const auto& layer : chart->semantic_layers)
            ordered.push_back(layer.id);
        std::swap(ordered[index], ordered[moving_up ? index - 1 : index + 1]);
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            reorder_semantic_layers_action_t{state.chart, std::move(ordered)});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer order was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic-layer order updated."
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer order updated, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing layer visibility.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a semantic layer before changing its visibility.";
        }
        const auto layer =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        if (layer == chart->semantic_layers.end()) {
            application_state.semantic_layer_target.reset();
            return "The selected semantic layer is no longer available.";
        }
        const auto was_visible = layer->visible;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                state.chart,
                layer->id,
                layer->name,
                {.red = static_cast<double>(layer->color.red) / 255.0,
                 .green = static_cast<double>(layer->color.green) / 255.0,
                 .blue = static_cast<double>(layer->color.blue) / 255.0,
                 .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                !was_visible,
                layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer visibility was unchanged."
                                               : applied.diagnostics.front().message;
        }
        if (was_visible) {
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? (was_visible ? "Semantic layer hidden." : "Semantic layer shown.")
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer visibility changed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target ||
            !visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target)) {
            return "Choose a visible semantic layer before filtering by it.";
        }
        if (application_state.semantic_layer_filter_chart != chart->id) {
            application_state.semantic_layer_filter_chart = chart->id;
            application_state.required_semantic_layer_filters.clear();
            application_state.excluded_semantic_layer_filters.clear();
        }
        const auto require =
            action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
        auto& enabled = require ? application_state.required_semantic_layer_filters
                                : application_state.excluded_semantic_layer_filters;
        auto& opposite = require ? application_state.excluded_semantic_layer_filters
                                 : application_state.required_semantic_layer_filters;
        const auto target = *application_state.semantic_layer_target;
        if (const auto found = std::ranges::find(enabled, target); found != enabled.end()) {
            enabled.erase(found);
            return require ? "Required semantic-layer filter removed."
                           : "Excluded semantic-layer filter removed.";
        }
        opposite.erase(std::remove(opposite.begin(), opposite.end(), target), opposite.end());
        enabled.push_back(target);
        return require ? "Semantic layer required in the view filter."
                       : "Semantic layer excluded from the view filter.";
    }
    case WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER:
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        return "Semantic-layer target cleared.";
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE: {
        if (editor_interaction_has_pending_gesture(state)) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            if (!result.accepted) {
                return compose_result_status(result, "Compose gesture canceled.");
            }
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before painting note membership.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer || layer->locked) {
            application_state.semantic_layer_paint_mode_active = false;
            return "Choose an unlocked visible semantic layer before painting note membership.";
        }
        application_state.semantic_layer_paint_mode_active =
            !application_state.semantic_layer_paint_mode_active;
        if (application_state.semantic_layer_paint_mode_active) {
            application_state.note_paint_mode_active = false;
        }
        return application_state.semantic_layer_paint_mode_active
                   ? "Semantic-layer paint mode enabled."
                   : "Semantic-layer paint mode disabled.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before soloing it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        application_state.semantic_layer_solo_active =
            !application_state.semantic_layer_solo_active;
        return application_state.semantic_layer_solo_active ? "Semantic-layer solo enabled."
                                                            : "Semantic-layer solo disabled.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing layer lock state.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before changing its lock state.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        const auto was_locked = layer->locked;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                .chart = state.chart,
                .layer = layer->id,
                .name = layer->name,
                .color = {.red = static_cast<double>(layer->color.red) / 255.0,
                          .green = static_cast<double>(layer->color.green) / 255.0,
                          .blue = static_cast<double>(layer->color.blue) / 255.0,
                          .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                .visible = layer->visible,
                .locked = !was_locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer lock state was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Semantic-layer lock changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        if (!was_locked) {
            application_state.semantic_layer_paint_mode_active = false;
        }
        return was_locked ? "Semantic layer unlocked." : "Semantic layer locked.";
    }
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before selecting by layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before selecting its notes.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        std::vector<object_id_t> notes;
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes) {
                if (std::ranges::find(note.semantic_layers, layer->id) !=
                    note.semantic_layers.end()) {
                    notes.push_back(note.id);
                }
            }
        }
        result =
            dispatch_editor_action(history, state, select_editor_notes_action_t{std::move(notes)});
        return compose_result_status(result, "Semantic-layer notes selected.");
    }
    case WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing semantic layers.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target || state.selected_notes.empty()) {
            return "Choose a visible semantic layer and select playable notes first.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        if (layer->locked) {
            return "Unlock the selected semantic layer before changing note membership.";
        }
        semantic_layer_edit_action_t edit;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER) {
            edit = assign_notes_to_semantic_layer_action_t{state.chart, layer->id,
                                                           state.selected_notes};
        } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER) {
            edit = remove_notes_from_semantic_layer_action_t{state.chart, layer->id,
                                                             state.selected_notes};
        } else {
            edit = toggle_semantic_layer_membership_action_t{state.chart, layer->id,
                                                             state.selected_notes};
        }
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer membership was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Semantic-layer membership changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "Semantic-layer membership updated.";
    }
    case WORM_UI_COMPOSE_ACTION_NONE:
        return {};
    }
    return compose_result_status(result, status);
}

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool {
    return state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value() ||
           state.pending_note_move_gesture.has_value() ||
           state.pending_note_paint_gesture.has_value() ||
           state.pending_semantic_layer_paint_gesture.has_value() ||
           state.pending_editor_area_selection.has_value() ||
           state.pending_space_range_selection.has_value();
}

auto selected_chart(const wormedit::document_history_t& history,
                    const wormedit::workspace_controller_t& workspace)
    -> const wormedit::chart_document_t* {
    if (!workspace.selected_chart) {
        return nullptr;
    }
    const auto found = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &wormedit::chart_document_t::id);
    return found == history.document.charts.end() ? nullptr : &*found;
}

auto visible_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t* {
    const auto found =
        std::ranges::find(chart.semantic_layers, id, &wormedit::semantic_layer_t::id);
    return found != chart.semantic_layers.end() && found->visible ? &*found : nullptr;
}

auto reject_application_command(
    std::string& status, std::string message,
    wormedit::diagnostic_code_t code = wormedit::diagnostic_code_t::unsupported_feature)
    -> wormedit::command_invocation_result_t {
    status = message;
    return {
        .diagnostics = {{.severity = wormedit::diagnostic_severity_t::error,
                         .code = code,
                         .message = std::move(message)}},
    };
}

auto accept_editor_command(const wormedit::editor_interaction_result_t& result, std::string& status,
                           std::string_view accepted) -> wormedit::command_invocation_result_t {
    status = compose_result_status(result, accepted);
    return {.accepted = result.accepted, .diagnostics = result.diagnostics};
}

auto invoke_application_command(
    const wormedit::command_invocation_context_t& invocation, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_state, std::string& status)
    -> wormedit::command_invocation_result_t {
    using namespace wormedit;
    const auto& id = invocation.command.value;
    const auto require_compose = [&]() -> editor_interaction_state_t* {
        return workspace.active_tab == workspace_tab_t::compose && workspace.selected_chart &&
                       compose_state && compose_state->chart == *workspace.selected_chart
                   ? &*compose_state
                   : nullptr;
    };
    const auto rebind_after_edit = [&]() -> command_invocation_result_t {
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return reject_application_command(
                status,
                rebound.diagnostics.empty() ? "Command applied, but workspace rebinding failed."
                                            : rebound.diagnostics.front().message,
                diagnostic_code_t::invalid_id);
        }
        return {.accepted = true};
    };

    if (id == "history.undo" || id == "history.redo") {
        if (compose_state) {
            (void)dispatch_editor_action(history, *compose_state, cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(history, *compose_state, select_editor_notes_action_t{});
        }
        const bool undo = id == "history.undo";
        const bool changed = undo ? undo_document_command(history) : redo_document_command(history);
        status = changed ? (undo ? "Undo applied." : "Redo applied.")
                         : (undo ? "Nothing to undo." : "Nothing to redo.");
        if (!changed) {
            return {.accepted = true};
        }
        timing_stage.reset();
        return rebind_after_edit();
    }

    if (id == "transport.play_pause") {
        if (!application_state.transport_play_pause) {
            return reject_application_command(status, "Chart preview is unavailable.");
        }
        status = application_state.transport_play_pause();
        return {.accepted = true};
    }

    if (id == "transport.stop") {
        if (!application_state.transport_stop || !application_state.transport_active ||
            !application_state.transport_active()) {
            return reject_application_command(status, "No audio preview is playing.");
        }
        application_state.transport_stop();
        status = "Audio preview stopped.";
        return {.accepted = true};
    }

    if (id == "edit.delete") {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status, "Delete requires the active Compose chart.");
        }
        const auto result = dispatch_editor_action(history, *state, delete_editor_notes_action_t{});
        auto invocation_result = accept_editor_command(
            result, status,
            result.document_changed ? "Note deletion applied." : "No note at the cursor.");
        if (result.document_changed) {
            const auto rebound = rebind_after_edit();
            if (!rebound) {
                return rebound;
            }
        }
        return invocation_result;
    }

    if (id == "selection.clear") {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status,
                                              "Clear Selection requires the active Compose chart.");
        }
        (void)dispatch_editor_action(history, *state, cancel_editor_gesture_action_t{});
        application_state.note_paint_mode_active = false;
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        return accept_editor_command(
            dispatch_editor_action(history, *state, select_editor_notes_action_t{}), status,
            "Selection, pending Compose gesture, and note-paint mode cleared.");
    }

    if (id == "selection.select_all" || id == "selection.invert") {
        auto* state = require_compose();
        const auto* chart = selected_chart(history, workspace);
        if (!state || !chart) {
            return reject_application_command(
                status, "Selection command requires the active Compose chart.");
        }
        std::unordered_set<std::uint64_t> previously_selected;
        if (id == "selection.invert") {
            previously_selected.reserve(state->selected_notes.size());
            for (const auto note : state->selected_notes) {
                previously_selected.insert(note.value);
            }
        }
        std::vector<object_id_t> notes;
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes) {
                if (id == "selection.select_all" || !previously_selected.contains(note.id.value)) {
                    notes.push_back(note.id);
                }
            }
        }
        return accept_editor_command(
            dispatch_editor_action(history, *state, select_editor_notes_action_t{std::move(notes)}),
            status,
            id == "selection.select_all" ? "All playable notes selected."
                                         : "Playable-note selection inverted.");
    }

    if (id == "navigation.previous_snap" || id == "navigation.next_snap") {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(status,
                                              "Snap navigation requires the active Compose chart.");
        }
        const auto direction = id == "navigation.previous_snap" ? interaction_direction_t::backward
                                                                : interaction_direction_t::forward;
        const auto result =
            dispatch_editor_action(history, *state, cycle_edit_snap_action_t{direction});
        if (result.accepted) {
            remember_snap_denominator(application_state, state->chart,
                                      state->active_snap_denominator);
        }
        return accept_editor_command(result, status,
                                     direction == interaction_direction_t::backward
                                         ? "Previous snap selected."
                                         : "Next snap selected.");
    }

    if (id == "navigation.previous_marker" || id == "navigation.next_marker") {
        auto* state = require_compose();
        if (!state) {
            return reject_application_command(
                status, "Bookmark navigation requires the active Compose chart.");
        }
        const auto direction = id == "navigation.previous_marker"
                                   ? marker_navigation_direction_t::previous
                                   : marker_navigation_direction_t::next;
        const auto marker = navigate_markers(history.document, history.document.revision,
                                             state->chart, state->cursor, direction);
        if (marker.status == marker_navigation_status_t::no_result) {
            status = direction == marker_navigation_direction_t::previous
                         ? "No previous beat-anchored bookmark or section boundary."
                         : "No next beat-anchored bookmark or section boundary.";
            return {.accepted = true};
        }
        if (!marker || !marker.item->beat_position) {
            const auto message = marker.diagnostics.empty()
                                     ? "Bookmark navigation could not resolve an exact beat row."
                                     : marker.diagnostics.front().message;
            return reject_application_command(status, message, diagnostic_code_t::invalid_id);
        }
        return accept_editor_command(
            dispatch_editor_action(
                history, *state,
                jump_edit_cursor_action_t{*marker.item->beat_position, marker.document_revision}),
            status,
            direction == marker_navigation_direction_t::previous
                ? "Moved to the previous bookmark or section boundary."
                : "Moved to the next bookmark or section boundary.");
    }

    constexpr std::array<std::pair<std::string_view, workspace_tab_t>, 7> tab_commands{{
        {"workspace.open_setup", workspace_tab_t::setup},
        {"workspace.open_metadata", workspace_tab_t::metadata},
        {"workspace.open_timing", workspace_tab_t::timing},
        {"workspace.open_compose", workspace_tab_t::compose},
        {"workspace.open_storyboard", workspace_tab_t::storyboard},
        {"workspace.open_audio_slices", workspace_tab_t::audio_slices},
        {"workspace.open_compare_validate", workspace_tab_t::compare_validate},
    }};
    const auto tab = std::ranges::find_if(
        tab_commands, [&](const auto& candidate) { return candidate.first == id; });
    if (tab != tab_commands.end()) {
        const auto applied =
            apply_workspace_action(workspace, history.document, history.document.revision,
                                   workspace_switch_tab_action_t{tab->second});
        status = applied ? "Workspace tab changed."
                         : (applied.diagnostics.empty() ? "Workspace tab change was rejected."
                                                        : applied.diagnostics.front().message);
        return {.accepted = applied.applied, .diagnostics = applied.diagnostics};
    }

    if (id == "timing.toggle_lock") {
        if (workspace.active_tab != workspace_tab_t::timing) {
            return reject_application_command(status,
                                              "Timing Lock requires the active Timing tab.");
        }
        application_state.timing_locked = !application_state.timing_locked;
        if (application_state.timing_locked && timing_stage) {
            cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
        }
        status =
            application_state.timing_locked ? "Timing edits locked." : "Timing edits unlocked.";
        return {.accepted = true};
    }

    if (id.starts_with("semantic_layer.select_slot_")) {
        auto* state = require_compose();
        const auto* chart = selected_chart(history, workspace);
        if (!state || !chart) {
            return reject_application_command(status,
                                              "Layer selection requires the active Compose chart.");
        }
        const auto suffix =
            std::string_view{id}.substr(std::string_view{"semantic_layer.select_slot_"}.size());
        std::size_t slot{};
        const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), slot);
        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() || slot == 0) {
            return reject_application_command(status, "That semantic-layer slot is unavailable.");
        }
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette =
            make_semantic_layer_palette_window(*chart, application_state.semantic_layer_target,
                                               application_state.semantic_layer_palette_page, false,
                                               WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette || slot > palette->layer_indices.size()) {
            return reject_application_command(status, "That semantic-layer slot is unavailable.");
        }
        const auto* layer = &chart->semantic_layers[palette->layer_indices[slot - 1]];
        application_state.semantic_layer_target = layer->id;
        status = "Semantic-layer target: " + layer->name;
        return {.accepted = true};
    }

    if (id == "semantic_layer.clear_target") {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        status = "Semantic-layer target cleared.";
        return {.accepted = true};
    }

    if (id == "semantic_layer.apply" || id == "semantic_layer.remove" ||
        id == "semantic_layer.toggle") {
        auto* state = require_compose();
        if (!state || !application_state.semantic_layer_target || state->selected_notes.empty()) {
            return reject_application_command(
                status, "Choose a semantic layer and select playable notes first.");
        }
        semantic_layer_edit_action_t action;
        if (id == "semantic_layer.apply") {
            action = assign_notes_to_semantic_layer_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        } else if (id == "semantic_layer.remove") {
            action = remove_notes_from_semantic_layer_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        } else {
            action = toggle_semantic_layer_membership_action_t{
                state->chart, *application_state.semantic_layer_target, state->selected_notes};
        }
        const auto applied = apply_semantic_layer_edit(history, history.document.revision, action);
        if (applied.applied) {
            const auto rebound = rebind_after_edit();
            if (!rebound) {
                return rebound;
            }
        }
        status = applied.applied
                     ? "Semantic-layer membership updated."
                     : (applied.diagnostics.empty() ? "Semantic-layer membership was unchanged."
                                                    : applied.diagnostics.front().message);
        return {.accepted = applied.applied || applied.diagnostics.empty(),
                .diagnostics = applied.diagnostics};
    }

    return reject_application_command(status, "Command is not implemented by the application.");
}

void refresh_application_command_states(
    wormedit::command_registry_t& registry, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    application_command_state_t& application_state) {
    using namespace wormedit;
    const auto* chart = selected_chart(history, workspace);
    const bool compose_ready = workspace.active_tab == workspace_tab_t::compose && chart &&
                               compose_state && compose_state->chart == chart->id;
    const bool pending = compose_ready && editor_interaction_has_pending_gesture(*compose_state);
    bool target_valid{};
    if (chart && application_state.semantic_layer_target) {
        const auto target_layer =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        target_valid = target_layer != chart->semantic_layers.end();
        if (!target_valid) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
        } else if (target_layer->locked || !target_layer->visible) {
            application_state.semantic_layer_paint_mode_active = false;
        }
    }
    const auto set = [&](std::string id, bool enabled, std::optional<bool> checked = std::nullopt) {
        (void)update_command_state(registry, command_id_t{std::move(id)}, enabled, checked);
    };
    set("document.new_project", false);
    set("document.new_chart", false);
    set("history.undo", !history.undo.empty());
    set("history.redo", !history.redo.empty());
    set("edit.delete", compose_ready);
    set("selection.clear", compose_ready && (pending || !compose_state->selected_objects.empty() ||
                                             application_state.note_paint_mode_active ||
                                             application_state.semantic_layer_paint_mode_active));
    set("selection.select_all", compose_ready);
    set("selection.invert", compose_ready);
    set("navigation.previous_marker", compose_ready);
    set("navigation.next_marker", compose_ready);
    set("navigation.previous_snap", compose_ready);
    set("navigation.next_snap", compose_ready);
    set("transport.play_pause",
        chart != nullptr && static_cast<bool>(application_state.transport_play_pause));
    set("transport.stop",
        application_state.transport_active && application_state.transport_active());

    constexpr std::array<std::pair<std::string_view, workspace_tab_t>, 7> tabs{{
        {"workspace.open_setup", workspace_tab_t::setup},
        {"workspace.open_metadata", workspace_tab_t::metadata},
        {"workspace.open_timing", workspace_tab_t::timing},
        {"workspace.open_compose", workspace_tab_t::compose},
        {"workspace.open_storyboard", workspace_tab_t::storyboard},
        {"workspace.open_audio_slices", workspace_tab_t::audio_slices},
        {"workspace.open_compare_validate", workspace_tab_t::compare_validate},
    }};
    for (const auto& [id, tab] : tabs) {
        set(std::string{id}, tab != workspace_tab_t::storyboard, workspace.active_tab == tab);
    }
    set("timing.toggle_lock", workspace.active_tab == workspace_tab_t::timing,
        application_state.timing_locked);
    set("timing.insert_bpm", false);
    set("timing.insert_sv", false);
    set("timing.open_settings", false);

    const auto palette = compose_ready
                             ? make_semantic_layer_palette_window(
                                   *chart, application_state.semantic_layer_target,
                                   application_state.semantic_layer_palette_chart == chart->id
                                       ? application_state.semantic_layer_palette_page
                                       : 0,
                                   false, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY)
                             : std::nullopt;
    for (std::size_t index = 0; index < WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY; ++index) {
        const auto* layer = palette && index < palette->layer_indices.size()
                                ? &chart->semantic_layers[palette->layer_indices[index]]
                                : nullptr;
        const bool available = layer != nullptr;
        const bool checked = available && application_state.semantic_layer_target &&
                             layer->id == *application_state.semantic_layer_target;
        set("semantic_layer.select_slot_" + std::to_string(index + 1), available, checked);
    }
    set("semantic_layer.clear_target", compose_ready && target_valid, !target_valid);
    const bool can_apply_layer =
        compose_ready && target_valid && !compose_state->selected_notes.empty();
    set("semantic_layer.apply", can_apply_layer);
    set("semantic_layer.remove", can_apply_layer);
    set("semantic_layer.toggle", can_apply_layer);
    set("audio.paint_hitsound", false);
}

auto application_command_focus(const WormUiWorkspaceAction& action,
                               const wormedit::workspace_controller_t& workspace)
    -> wormedit::command_focus_t {
    using namespace wormedit;
    command_focus_t focus{
        .text_entry_active = action.shortcut_text_entry_active,
        .ime_composition_active = action.shortcut_ime_composition_active,
        .workspace_active = action.shortcut_window_focused,
        .active_tab = workspace.active_tab,
        .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::metadata, workspace_tab_t::timing,
                         workspace_tab_t::compose, workspace_tab_t::storyboard,
                         workspace_tab_t::audio_slices, workspace_tab_t::compare_validate},
    };
    if (action.shortcut_compose_lane_focused && action.shortcut_focused_lane_id != 0) {
        focus.focused_lane = lane_id_t{action.shortcut_focused_lane_id};
    }
    return focus;
}

constexpr auto application_primary_modifier() -> wormedit::shortcut_primary_modifier_t {
#if defined(__APPLE__)
    return wormedit::shortcut_primary_modifier_t::meta;
#else
    return wormedit::shortcut_primary_modifier_t::control;
#endif
}

auto dispatch_application_shortcuts(
    const WormUiWorkspaceAction& action, wormedit::command_registry_t& registry,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    application_command_state_t& application_state, std::string& status) -> bool {
    using namespace wormedit;
    if (action.shortcut_overflowed ||
        action.shortcut_event_count > WORM_UI_SHORTCUT_EVENT_CAPACITY) {
        status = "Shortcut input exceeded its per-frame capacity and was discarded.";
        return true;
    }
    bool handled{};
    for (std::size_t index = 0; index < action.shortcut_event_count; ++index) {
        refresh_application_command_states(registry, history, workspace, compose_state,
                                           application_state);
        const auto& input = action.shortcut_events[index];
        const auto dispatched = dispatch_shortcut(
            registry,
            {.chord = {.key = static_cast<shortcut_key_t>(input.hid_usage),
                       .modifiers = input.modifiers},
             .auto_repeat = input.repeat},
            application_command_focus(action, workspace), application_primary_modifier());
        if (dispatched.status == command_dispatch_status_t::invoked) {
            handled = true;
        } else if (dispatched.status == command_dispatch_status_t::disabled) {
            handled = true;
            status = dispatched.command
                         ? "Command is currently unavailable: " + dispatched.command->value
                         : "Command is currently unavailable.";
        } else if (dispatched.status == command_dispatch_status_t::conflict ||
                   dispatched.status == command_dispatch_status_t::invalid_event ||
                   dispatched.status == command_dispatch_status_t::rejected) {
            handled = true;
            status = dispatched.diagnostics.empty() ? "Shortcut command was rejected."
                                                    : dispatched.diagnostics.front().message;
        } else if (dispatched.status == command_dispatch_status_t::repeat_suppressed) {
            handled = true;
        }
    }
    return handled;
}

} // namespace

int main(int argc, char** argv) {
    constexpr int maximum_startup_paths = 30;
    if (argc > maximum_startup_paths + 1) {
        std::fprintf(stderr, "usage: wormedit [project.wormedit [recovery-artifact ...]]\n");
        return 2;
    }
    wormedit::native_project_startup_request_t startup_request;
    if (argc > 1)
        startup_request.primary_path = std::filesystem::path{argv[1]};
    for (int index = 2; index < argc; ++index)
        startup_request.recovery_entries.emplace_back(argv[index]);
    const auto requested_primary_path = startup_request.primary_path;

    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return 1;
    }
    const auto platform_recovery = wormedit::prepare_platform_recovery_directory();
    if (platform_recovery) {
        startup_request.recovery_directories.push_back(platform_recovery.path);
    }
    auto startup = wormedit::prepare_native_project_startup(
        initial_document(new_session_project_id()), std::move(startup_request));

    WormAudioSystem audio{};
    if (!worm_audio_init(&audio)) {
        std::fprintf(stderr, "Audio subsystem init failed\n");
        SDL_Quit();
        return 1;
    }

    WormWindow window{};
    const WormWindowDesc window_desc{"wormedit", 1280, 720};
    if (!worm_window_create(&window, &window_desc)) {
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    if (!worm_ui_frontend_init(window.handle)) {
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    auto history =
        wormedit::make_document_history(std::move(startup.document), 128, startup.loaded_primary);
    auto binding = wormedit::make_workspace_controller(history);
    if (!binding) {
        std::fprintf(stderr, "Initial workspace binding failed\n");
        worm_ui_frontend_shutdown();
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }
    auto workspace = std::move(*binding.workspace);
    const auto initial_autosave_sequence = wormedit::native_project_recovery_session_sequence(
        startup.recovery_scan, history.document.id);
    wormedit::task_scheduler_t autosave_scheduler(1, 1);
    wormedit::native_project_autosave_loop_t autosave_loop(
        wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_scheduler,
        startup.primary_version, initial_autosave_sequence);
    std::string status;
    if (startup.loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else if (platform_recovery) {
        status = "New unsaved workspace ready; platform recovery scan complete.";
    } else {
        status = "New unsaved workspace ready; recovery discovery unavailable.";
    }
    if (!platform_recovery && !platform_recovery.error.empty()) {
        status += " " + platform_recovery.error;
    }
    std::optional<wormedit::audio_offset_drag_t> audio_offset_drag;
    std::optional<wormedit::workspace_tab_t> audio_offset_drag_tab;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    wormedit::audio::decoded_source_registry_t audition_sources;
    wormedit::audio::decoded_block_cache_t waveform_blocks;
    wormedit::audio::waveform_cache_t waveform_cache;
    wormedit::task_scheduler_t waveform_scheduler(1, 4);
    wormedit::audio::waveform_job_t waveform_job{audition_sources, waveform_blocks, waveform_cache,
                                                 waveform_scheduler};
    std::optional<wormedit::asset_id_t> waveform_job_asset;
    std::shared_ptr<const wormedit::audio::waveform_analysis_result_t> waveform_analysis;
    wormedit::audio::decoded_block_cache_t audition_cache;
    wormedit::audio::transport_t audition_transport{audition_cache};
    wormedit::audio::audition_transport_bridge_t audition_bridge{audition_transport};
    wormedit::audio::object_audition_controller_t object_audition{audition_bridge};
    wormedit::audio::audition_session_t audition_session{audition_sources, audition_transport,
                                                         audition_bridge};
    wormedit::sys::sdl_audition_output_t audition_output{audition_session};
    wormedit::audio::decoded_block_cache_t preview_cache;
    wormedit::audio::transport_t preview_transport{preview_cache};
    wormedit::audio::preview_transport_bridge_t preview_bridge{preview_transport};
    wormedit::audio::preview_session_t preview_session{audition_sources, preview_transport,
                                                       preview_bridge};
    wormedit::sys::sdl_preview_output_t preview_output{preview_session};
    std::optional<wormedit::preview_schedule_t> active_preview_schedule;
    std::optional<wormedit::timing_index_t> active_preview_timing;
    std::vector<wormedit::audio::preview_object_source_binding_t> active_preview_bindings;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> preview_metronome_bindings;
    std::optional<wormedit::chart_id_t> active_preview_chart;
    std::uint64_t active_preview_revision{};
    std::uint64_t preview_resume_frame{};
    std::uint64_t preview_end_frame{};
    bool preview_paused{};
    const auto asset_base =
        startup.loaded_primary ? requested_primary_path.parent_path() : std::filesystem::path{};
    using audition_asset_entry_t = std::pair<wormedit::asset_id_t, std::filesystem::path>;
    std::optional<wormedit::song_id_t> audition_source_project;
    std::vector<audition_asset_entry_t> audition_asset_inventory;
    std::uint64_t audition_source_generation{};
    const auto reconcile_audition_sources = [&](bool stop_playback) -> std::optional<std::size_t> {
        std::vector<audition_asset_entry_t> next_inventory;
        next_inventory.reserve(history.document.audio_assets.size());
        for (const auto& asset : history.document.audio_assets) {
            next_inventory.emplace_back(asset.id, asset.source_path);
        }
        if (audition_source_project == history.document.id &&
            audition_asset_inventory == next_inventory) {
            return std::nullopt;
        }
        if (stop_playback) {
            static_cast<void>(audition_output.stop());
            static_cast<void>(preview_output.stop());
            preview_resume_frame = 0;
            preview_end_frame = 0;
            preview_paused = false;
            active_preview_schedule.reset();
            active_preview_timing.reset();
            active_preview_bindings.clear();
            active_preview_chart.reset();
        }
        waveform_job.cancel();
        waveform_job_asset.reset();
        waveform_analysis.reset();
        audition_sources.clear();
        waveform_blocks.clear();
        waveform_cache.clear();
        preview_metronome_bindings.clear();
        ++audition_source_generation;
        std::size_t unavailable{};
        for (const auto& asset : history.document.audio_assets) {
            if (asset.source_path.empty() ||
                (!asset.source_path.is_absolute() && asset_base.empty())) {
                ++unavailable;
                continue;
            }
            const auto resolved_path = asset.source_path.is_absolute()
                                           ? asset.source_path
                                           : (asset_base / asset.source_path).lexically_normal();
            const auto registered = audition_sources.register_source({
                .asset = asset.id,
                .path = resolved_path,
                .source =
                    {
                        "project:" + std::to_string(history.document.id.value) +
                            ":asset:" + std::to_string(asset.id.value),
                        "binding:" + std::to_string(audition_source_generation) + ":" +
                            resolved_path.generic_string(),
                    },
            });
            if (!registered) {
                ++unavailable;
            }
        }
        constexpr std::array click_frequencies{1'100.0, 1'500.0, 1'900.0};
        constexpr std::array click_gains{0.16F, 0.23F, 0.31F};
        constexpr auto click_frames = wormedit::audio::output_sample_rate * 45U / 1'000U;
        constexpr auto pi = 3.14159265358979323846;
        for (std::size_t accent = 0; accent < click_frequencies.size(); ++accent) {
            std::vector<float> pcm(click_frames * wormedit::audio::output_channel_count);
            for (std::size_t frame = 0; frame < click_frames; ++frame) {
                const auto seconds = static_cast<double>(frame) /
                                     static_cast<double>(wormedit::audio::output_sample_rate);
                const auto envelope = std::exp(-90.0 * seconds);
                const auto sample =
                    click_gains[accent] *
                    static_cast<float>(envelope *
                                       std::sin(2.0 * pi * click_frequencies[accent] * seconds));
                const auto first_sample = frame * wormedit::audio::output_channel_count;
                pcm[first_sample] = sample;
                pcm[first_sample + 1] = sample;
            }
            const auto generated = audition_sources.register_generated_source({
                .source = {"internal:metronome:" + std::to_string(accent),
                           "binding:" + std::to_string(audition_source_generation)},
                .interleaved_stereo = std::move(pcm),
            });
            if (!generated) {
                preview_metronome_bindings.clear();
                break;
            }
            preview_metronome_bindings.push_back({
                .accent = static_cast<wormedit::metronome_accent_t>(accent),
                .voice = {.source = generated.binding->source,
                          .source_start_frame = 0,
                          .frame_count = generated.binding->output_frame_count,
                          .gain = 1.0F},
            });
        }
        audition_source_project = history.document.id;
        audition_asset_inventory = std::move(next_inventory);
        return unavailable;
    };
    const auto initial_unavailable_audio_assets = reconcile_audition_sources(false).value_or(0);
    if (initial_unavailable_audio_assets != 0) {
        status += " " + std::to_string(initial_unavailable_audio_assets) +
                  " audio asset(s) are unavailable for preview.";
    }
    std::string audition_output_error;
    if (!audition_output.open(&audition_output_error)) {
        status += " Audio output unavailable: " + audition_output_error;
    }
    std::string preview_output_error;
    if (!preview_output.open(&preview_output_error)) {
        status += " Chart preview output unavailable: " + preview_output_error;
    }
#endif
    wormedit::native_project_recovery_controller_t recovery_controller;
    if (startup.recovery_scan_configured)
        (void)recovery_controller.replace_scan(std::move(startup.recovery_scan));
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    const auto scanned_primary_version = startup.primary_version;

    timing_scene_cache_t timing_scene_cache;
    std::optional<wormedit::timing_lane_pseudonote_preview_t> timing_stage;
    std::optional<std::pair<wormedit::chart_id_t, std::uint64_t>> timing_recipe_selection_binding;
    std::optional<wormedit::sv_recipe_draft_t> timing_recipe_draft;
    std::optional<wormedit::sv_recipe_draft_preview_t> timing_recipe_preview;
    compose_scene_cache_t compose_scene_cache;
    std::optional<wormedit::editor_interaction_state_t> compose_interaction_state;
    wormedit::viewport_input_controller_t compose_viewport_input;
    wormedit::editor_navigation_input_controller_t compose_navigation_input;
    application_command_state_t application_command_state;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    application_command_state.transport_active = [&] {
        return audition_session.active() || preview_session.active() || preview_paused;
    };
    application_command_state.transport_play_pause = [&]() -> std::string {
        if (preview_session.active()) {
            preview_resume_frame = preview_session.timeline_frame();
            static_cast<void>(preview_output.stop());
            preview_paused = true;
            return "Chart preview paused.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (chart == nullptr)
            return "Select a chart before starting preview.";

        const bool reusable = active_preview_schedule.has_value() &&
                              active_preview_chart == chart->id &&
                              active_preview_revision == history.document.revision;
        if (!reusable) {
            const auto timing = wormedit::build_timing_index(*chart);
            if (!timing)
                return timing.diagnostics.empty() ? "Chart timing could not be compiled."
                                                  : timing.diagnostics.front().message;
            const auto schedule = wormedit::build_preview_schedule(
                *chart, *timing.index,
                {.sample_rate = wormedit::audio::output_sample_rate,
                 .include_metronome = !preview_metronome_bindings.empty()});
            if (!schedule)
                return schedule.diagnostics.empty() ? "Chart preview could not be compiled."
                                                    : schedule.diagnostics.front().message;
            const auto resolved = wormedit::audio::resolve_preview_bindings(
                history.document, chart->id, *schedule.schedule, audition_sources.bindings());
            if (!resolved)
                return resolved.diagnostics.empty() ? "Chart audio could not be resolved."
                                                    : resolved.diagnostics.front().message;
            active_preview_schedule = std::move(*schedule.schedule);
            active_preview_timing = std::move(*timing.index);
            active_preview_bindings = std::move(resolved.bindings);
            active_preview_chart = chart->id;
            active_preview_revision = history.document.revision;
            preview_resume_frame = 0;
            preview_end_frame = 0;
            const auto include_event_ends = [&](const auto& events) {
                for (const auto& event : events) {
                    const auto binding = std::ranges::lower_bound(
                        active_preview_bindings, event.object, {},
                        &wormedit::audio::preview_object_source_binding_t::object);
                    if (binding == active_preview_bindings.end() ||
                        binding->object != event.object) {
                        continue;
                    }
                    for (const auto& voice : binding->voices) {
                        if (voice.frame_count <=
                            std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
                            preview_end_frame =
                                std::max(preview_end_frame, event.sample_frame + voice.frame_count);
                        }
                    }
                }
            };
            include_event_ends(active_preview_schedule->playable);
            include_event_ends(active_preview_schedule->bgm);
            for (const auto& event : active_preview_schedule->metronome) {
                const auto binding =
                    std::ranges::find(preview_metronome_bindings, event.accent,
                                      &wormedit::audio::preview_metronome_source_binding_t::accent);
                if (binding != preview_metronome_bindings.end() &&
                    binding->voice.frame_count <=
                        std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
                    preview_end_frame = std::max(preview_end_frame,
                                                 event.sample_frame + binding->voice.frame_count);
                }
            }
        }

        static_cast<void>(audition_output.stop());
        const auto started = preview_output.start(*active_preview_schedule, active_preview_bindings,
                                                  preview_metronome_bindings, preview_resume_frame);
        if (!started)
            return started.message.empty() ? "Chart preview could not start." : started.message;
        preview_paused = false;
        return preview_resume_frame == 0 ? "Chart preview started." : "Chart preview resumed.";
    };
    application_command_state.transport_seek = [&](std::uint64_t frame) -> std::string {
        if (!active_preview_schedule || !active_preview_chart ||
            active_preview_revision != history.document.revision || !workspace.selected_chart ||
            *workspace.selected_chart != *active_preview_chart || frame > preview_end_frame) {
            return "Chart preview seek is unavailable or outside the compiled timeline.";
        }
        static_cast<void>(audition_output.stop());
        if (preview_session.active()) {
            const auto sought = preview_output.seek(frame);
            if (!sought)
                return sought.message.empty() ? "Chart preview seek failed." : sought.message;
            preview_paused = false;
        } else {
            preview_paused = true;
        }
        preview_resume_frame = frame;
        return "Chart preview moved to " +
               std::to_string(frame / wormedit::audio::output_sample_rate) + " seconds.";
    };
    application_command_state.transport_stop = [&] {
        static_cast<void>(audition_output.stop());
        static_cast<void>(preview_output.stop());
        preview_resume_frame = 0;
        preview_paused = false;
    };
#endif
    timing_grid_state_t timing_grid;
    auto command_registry = wormedit::make_default_editor_command_registry(
        [&](const wormedit::command_invocation_context_t& invocation) {
            return invoke_application_command(invocation, history, workspace,
                                              compose_interaction_state, timing_stage,
                                              application_command_state, status);
        });
    const auto shortcut_startup = wormedit::load_platform_shortcut_profile(command_registry);
    if (!shortcut_startup.diagnostics.empty()) {
        status += " " + shortcut_startup.diagnostics.front().message;
    }
    bool running = true;
    auto compose_viewport_last_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
    while (running) {
        SDL_Event event;
        worm_ui_frontend_begin_input();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
            worm_ui_frontend_handle_event(&event);
        }
        worm_ui_frontend_end_input();
        const auto compose_viewport_now_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
        const auto compose_viewport_delta_seconds =
            compose_viewport_now_ticks >= compose_viewport_last_ticks
                ? static_cast<double>(compose_viewport_now_ticks - compose_viewport_last_ticks) /
                      1000.0
                : 0.0;
        compose_viewport_last_ticks = compose_viewport_now_ticks;

        const auto autosave_target = wormedit::make_native_project_autosave_target(
            history.document.id, startup.loaded_primary, requested_primary_path,
            platform_recovery.path);
        if (const auto autosave_result = autosave_loop.tick(
                history.document, wormedit::is_document_dirty(history),
                wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_target)) {
            const auto autosave_text = autosave_status_text(*autosave_result);
            if (!autosave_text.empty())
                status = autosave_text;
        }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        const bool preview_binding_current =
            active_preview_chart && active_preview_revision == history.document.revision &&
            workspace.selected_chart && *workspace.selected_chart == *active_preview_chart;
        if (active_preview_chart && !preview_binding_current) {
            const bool interrupted = preview_session.active() || preview_paused;
            if (preview_session.active())
                static_cast<void>(preview_output.stop());
            preview_resume_frame = 0;
            preview_end_frame = 0;
            preview_paused = false;
            active_preview_schedule.reset();
            active_preview_timing.reset();
            active_preview_bindings.clear();
            active_preview_chart.reset();
            if (interrupted)
                status = "Chart preview stopped because its document or selected chart changed.";
        }
        if (const auto serviced = audition_output.service(); serviced && !*serviced) {
            static_cast<void>(audition_output.stop());
            if (serviced->status == wormedit::sys::sdl_audition_service_status_t::device_failed) {
                status = "Audio preview stopped after an output-device failure.";
            } else if (serviced->status ==
                       wormedit::sys::sdl_audition_service_status_t::render_failed) {
                status = "Audio preview stopped after a realtime cache underrun.";
            } else if (serviced->pump &&
                       serviced->pump->status ==
                           wormedit::audio::audition_pump_status_t::source_unavailable) {
                status = "Audio preview stopped because its decoded source is unavailable.";
            } else {
                status = "Audio preview stopped after a bounded lookahead failure.";
            }
        }
        if (const auto serviced = preview_output.service(); serviced && !*serviced) {
            static_cast<void>(preview_output.stop());
            preview_resume_frame = 0;
            preview_paused = false;
            if (serviced->status == wormedit::sys::sdl_preview_service_status_t::device_failed) {
                status = "Chart preview stopped after an output-device failure.";
            } else if (serviced->status ==
                       wormedit::sys::sdl_preview_service_status_t::render_failed) {
                status = "Chart preview stopped after a realtime cache underrun.";
            } else if (serviced->pump && serviced->pump->decode &&
                       serviced->pump->decode->status ==
                           wormedit::audio::preview_pump_status_t::source_unavailable) {
                status = "Chart preview stopped because a decoded source is unavailable.";
            } else {
                status = "Chart preview stopped after a bounded refill failure.";
            }
        }
        if (!preview_session.active() && !preview_paused)
            preview_resume_frame = 0;
        application_command_state.preview_playhead_absolute_row.reset();
        if (active_preview_timing && (preview_session.active() || preview_paused)) {
            const auto frame =
                preview_session.active() ? preview_session.timeline_frame() : preview_resume_frame;
            if (frame <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                const auto position = wormedit::musical_position_at(
                    *active_preview_timing,
                    {static_cast<std::int64_t>(frame), wormedit::audio::output_sample_rate});
                if (position && position.absolute_row->denominator != 0) {
                    const auto row = static_cast<double>(position.absolute_row->numerator) /
                                     static_cast<double>(position.absolute_row->denominator);
                    if (std::isfinite(row))
                        application_command_state.preview_playhead_absolute_row = row;
                }
            }
        }
        if (workspace.transport.audio_asset != waveform_job_asset) {
            waveform_job.cancel();
            waveform_job_asset.reset();
            waveform_analysis.reset();
            if (workspace.transport.audio_asset) {
                const auto started = waveform_job.request(*workspace.transport.audio_asset, 256);
                if (started) {
                    waveform_job_asset = workspace.transport.audio_asset;
                } else if (started.status !=
                           wormedit::audio::waveform_job_start_status_t::capacity_exceeded) {
                    status = started.message.empty() ? "Waveform analysis could not start."
                                                     : started.message;
                }
            }
        }
        if (const auto completed = waveform_job.poll();
            completed && waveform_job_asset == completed->asset) {
            if (*completed) {
                waveform_analysis = completed->analysis;
            } else if (completed->task_error) {
                status = completed->task_error->message;
            }
        }
#endif

        if (compose_interaction_state) {
            const auto& state = *compose_interaction_state;
            const bool selected_chart_changed =
                !workspace.selected_chart || *workspace.selected_chart != state.chart;
            const bool pending_is_stale =
                (state.pending_hold &&
                 state.pending_hold->document_revision != history.document.revision) ||
                (state.pending_note_duration_gesture &&
                 state.pending_note_duration_gesture->document_revision !=
                     history.document.revision) ||
                (state.pending_note_move_gesture &&
                 state.pending_note_move_gesture->document_revision != history.document.revision) ||
                (state.pending_note_paint_gesture &&
                 state.pending_note_paint_gesture->document_revision !=
                     history.document.revision) ||
                (state.pending_semantic_layer_paint_gesture &&
                 state.pending_semantic_layer_paint_gesture->document_revision !=
                     history.document.revision) ||
                (state.pending_editor_area_selection &&
                 state.pending_editor_area_selection->document_revision !=
                     history.document.revision) ||
                (state.pending_space_range_selection &&
                 state.pending_space_range_selection->document_revision !=
                     history.document.revision);
            const bool compose_inactive =
                workspace.active_tab != wormedit::workspace_tab_t::compose;
            if (editor_interaction_has_pending_gesture(state) &&
                (selected_chart_changed || pending_is_stale || compose_inactive)) {
                (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                       wormedit::cancel_editor_gesture_action_t{});
            }
            if (selected_chart_changed) {
                compose_interaction_state.reset();
            }
        }

        const auto model = wormedit::make_workspace_view(workspace, history.document);
        if (timing_stage &&
            (timing_stage->document_revision != history.document.revision ||
             workspace.active_tab != wormedit::workspace_tab_t::timing ||
             !workspace.selected_chart || *workspace.selected_chart != timing_stage->chart)) {
            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
        }
        if (timing_recipe_draft &&
            (timing_recipe_draft->document_revision != history.document.revision ||
             workspace.active_tab != wormedit::workspace_tab_t::timing ||
             !workspace.selected_chart || *workspace.selected_chart != timing_recipe_draft->chart ||
             application_command_state.timing_locked)) {
            wormedit::cancel_sv_recipe_draft(*timing_recipe_draft);
            timing_recipe_draft.reset();
            timing_recipe_preview.reset();
            timing_recipe_selection_binding.reset();
        }
        if (timing_recipe_selection_binding &&
            (workspace.active_tab != wormedit::workspace_tab_t::timing ||
             application_command_state.timing_locked || !workspace.selected_chart ||
             *workspace.selected_chart != timing_recipe_selection_binding->first ||
             history.document.revision != timing_recipe_selection_binding->second)) {
            timing_recipe_selection_binding.reset();
        }
        if (!model) {
            status = model.diagnostics.empty() ? "Workspace view unavailable"
                                               : model.diagnostics.front().message;
        } else {
            if (workspace.selected_chart) {
                const auto chart =
                    std::ranges::find(history.document.charts, *workspace.selected_chart,
                                      &wormedit::chart_document_t::id);
                if (chart != history.document.charts.end()) {
                    timing_grid = refresh_timing_grid(*chart, application_command_state);
                }
            }
            if (audio_offset_drag &&
                (!audio_offset_drag_tab || workspace.active_tab != *audio_offset_drag_tab ||
                 !workspace.selected_chart ||
                 *workspace.selected_chart != audio_offset_drag->chart ||
                 history.document.revision != audio_offset_drag->document_revision ||
                 application_command_state.timing_locked)) {
                audio_offset_drag.reset();
                audio_offset_drag_tab.reset();
            }
            auto flattened =
                flatten_workspace(*model.view, history.document, status, recovery_controller.view(),
                                  recovery_scan_configured);
            flattened.view.metadata.timing_locked = application_command_state.timing_locked;
            flattened.view.metadata.audio_waveform_has_selected_asset =
                workspace.transport.audio_asset.has_value();
            flattened.view.metadata.audio_waveform_selected_asset_id =
                workspace.transport.audio_asset ? workspace.transport.audio_asset->value : 0;
            auto& audio_view = flattened.view.audio_slices;
            audio_view.document_revision = history.document.revision;
            audio_view.timing_locked = application_command_state.timing_locked;
            if (workspace.selected_chart) {
                const auto selected =
                    std::ranges::find(history.document.charts, *workspace.selected_chart,
                                      &wormedit::chart_document_t::id);
                if (selected != history.document.charts.end()) {
                    audio_view.has_chart = true;
                    audio_view.chart_id = selected->id.value;
                    audio_view.offset_numerator =
                        selected->beat_zero_audio_offset_seconds.numerator;
                    audio_view.offset_denominator =
                        selected->beat_zero_audio_offset_seconds.denominator;
                }
            }
            if (audio_offset_drag) {
                audio_view.offset_drag_active = true;
                audio_view.offset_drag_delta_frames = audio_offset_drag->delta_frames;
                audio_view.proposed_offset_numerator =
                    audio_offset_drag->proposed_offset_seconds.numerator;
                audio_view.proposed_offset_denominator =
                    audio_offset_drag->proposed_offset_seconds.denominator;
                flattened.view.metadata.audio_offset_drag_active = true;
                flattened.view.metadata.audio_offset_drag_delta_frames =
                    audio_offset_drag->delta_frames;
                flattened.view.metadata.proposed_audio_offset_numerator =
                    audio_offset_drag->proposed_offset_seconds.numerator;
                flattened.view.metadata.proposed_audio_offset_denominator =
                    audio_offset_drag->proposed_offset_seconds.denominator;
            }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            flattened.view.transport_available =
                workspace.selected_chart.has_value() && preview_output.available();
            flattened.view.transport_playing =
                preview_session.active() || audition_session.active();
            flattened.view.transport_paused = preview_paused;
            flattened.view.transport_frame =
                preview_session.active()    ? preview_session.timeline_frame()
                : audition_session.active() ? audition_session.timeline_frame()
                                            : preview_resume_frame;
            flattened.view.transport_end_frame = preview_end_frame;
            flattened.view.transport_sample_rate = wormedit::audio::output_sample_rate;
#else
            flattened.view.transport_available = false;
            flattened.view.transport_playing = false;
            flattened.view.transport_paused = false;
            flattened.view.transport_frame = 0;
            flattened.view.transport_end_frame = 0;
            flattened.view.transport_sample_rate = 44'100;
#endif
            for (std::size_t line = 0; line < flattened.view.summary_line_count; ++line) {
                flattened.view.summary_lines[line] = flattened.summary_storage[line].c_str();
            }
            for (std::size_t row = 0; row < flattened.view.recovery_candidate_count; ++row) {
                flattened.view.recovery_candidates[row].path_label =
                    flattened.recovery_path_storage[row].c_str();
                flattened.view.recovery_candidates[row].summary =
                    flattened.recovery_summary_storage[row].c_str();
                flattened.view.recovery_candidates[row].disabled_reason =
                    flattened.recovery_disabled_storage[row].empty()
                        ? nullptr
                        : flattened.recovery_disabled_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0; diagnostic < flattened.view.recovery_diagnostic_count;
                 ++diagnostic) {
                flattened.view.recovery_diagnostics[diagnostic] =
                    flattened.recovery_diagnostic_storage[diagnostic].c_str();
            }
            if (flattened.view.audio_slices.visible) {
                flattened.view.audio_slices.asset_ids = flattened.audio_asset_storage.data();
            }
            flattened.view.compose.branch_rows = flattened.compose_branch_row_storage.data();
            for (std::size_t row = 0; row < flattened.view.compose.branch_row_count; ++row) {
                flattened.compose_branch_row_storage[row].display_text =
                    flattened.compose_branch_text_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0;
                 diagnostic < flattened.view.compose.branch_diagnostic_count; ++diagnostic) {
                flattened.view.compose.branch_diagnostics[diagnostic] =
                    flattened.compose_branch_diagnostic_storage[diagnostic].c_str();
            }
            if (flattened.view.metadata.visible) {
                flattened.view.metadata.search_tags =
                    flattened.view.metadata.search_tags_editable
                        ? flattened.metadata_search_tag_storage.data()
                        : nullptr;
                flattened.view.metadata.search_tag_bytes =
                    flattened.view.metadata.search_tags_editable
                        ? flattened.metadata_search_tag_length_storage.data()
                        : nullptr;
                flattened.view.metadata.format_fields =
                    flattened.metadata_format_field_storage.data();
                flattened.view.metadata.format_targets =
                    flattened.metadata_format_target_storage.data();
            }
            int compose_window_width = 0;
            int compose_window_height = 0;
            worm_window_get_size(&window, &compose_window_width, &compose_window_height);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (flattened.view.audio_slices.visible || flattened.view.metadata.visible) {
                auto& audio = flattened.view.audio_slices;
                audio.analysis_pending = waveform_job.busy();
                flattened.view.metadata.audio_waveform_analysis_pending = waveform_job.busy();
                if (waveform_analysis && waveform_analysis->waveform) {
                    const auto& waveform = *waveform_analysis->waveform;
                    const auto available_width = std::max(compose_window_width - 80, 1);
                    const auto pixel_width =
                        std::min(static_cast<std::size_t>(available_width),
                                 static_cast<std::size_t>(WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY));
                    const auto projection = wormedit::audio::project_waveform(
                        waveform, {.begin_frame = 0,
                                   .end_frame = waveform.total_frames,
                                   .pixel_width = pixel_width,
                                   .maximum_columns = WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY});
                    if (projection) {
                        flattened.audio_waveform_storage.reserve(projection.columns.size());
                        for (const auto& column : projection.columns) {
                            flattened.audio_waveform_storage.push_back({
                                .minimum = {column.minimum[0], column.minimum[1]},
                                .maximum = {column.maximum[0], column.maximum[1]},
                                .absolute_peak = {column.absolute_peak[0], column.absolute_peak[1]},
                                .rms = {column.rms[0], column.rms[1]},
                            });
                        }
                        audio.waveform_available = true;
                        audio.total_frames = waveform.total_frames;
                        audio.sample_rate = waveform.sample_rate;
                        audio.waveform_columns = flattened.audio_waveform_storage.data();
                        audio.waveform_column_count = flattened.audio_waveform_storage.size();
                        auto& metadata = flattened.view.metadata;
                        metadata.audio_waveform_available = true;
                        metadata.audio_waveform_total_frames = waveform.total_frames;
                        metadata.audio_waveform_sample_rate = waveform.sample_rate;
                        metadata.audio_waveform_columns = flattened.audio_waveform_storage.data();
                        metadata.audio_waveform_column_count =
                            flattened.audio_waveform_storage.size();
                    }
                }
            }
#endif
            populate_timing_view(flattened, history, workspace, timing_scene_cache,
                                 compose_window_width, compose_window_height, timing_grid,
                                 timing_stage ? &*timing_stage : nullptr,
                                 timing_recipe_draft ? &*timing_recipe_draft : nullptr,
                                 timing_recipe_preview ? &*timing_recipe_preview : nullptr,
                                 timing_recipe_selection_binding.has_value());
            populate_compose_view(flattened, history, workspace, compose_scene_cache,
                                  compose_window_width, compose_window_height,
                                  compose_interaction_state, application_command_state);
            const auto action = worm_ui_frontend_compose(&flattened.view);
            std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (waveform_analysis && waveform_analysis->waveform && waveform_job_asset &&
                workspace.transport.audio_asset == waveform_job_asset) {
                selected_waveform_sample_rate = waveform_analysis->waveform->sample_rate;
            }
#endif
            const bool shortcut_handled =
                action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                dispatch_application_shortcuts(action, command_registry, history, workspace,
                                               compose_interaction_state, application_command_state,
                                               status);
            if (shortcut_handled && compose_interaction_state &&
                editor_interaction_has_pending_gesture(*compose_interaction_state)) {
                (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                       wormedit::cancel_editor_gesture_action_t{});
            }
            if (shortcut_handled) {
                audio_offset_drag.reset();
                audio_offset_drag_tab.reset();
            }
            if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                (action.compose_input_count != 0 || action.compose_viewport_input_count != 0 ||
                 action.compose_has_pointer_hover ||
                 (workspace.active_tab == wormedit::workspace_tab_t::compose &&
                  action.compose_input_chart_id != 0)) &&
                compose_interaction_state) {
                status = dispatch_compose_input_action(
                    action, history, workspace, *compose_interaction_state, compose_viewport_input,
                    compose_navigation_input, application_command_state, compose_scene_cache,
                    compose_viewport_delta_seconds);
                if (auto request =
                        std::exchange(compose_scene_cache.audition_request, std::nullopt)) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                    if (request->object.kind != wormedit::editor_selection_object_kind_t::note &&
                        request->object.kind !=
                            wormedit::editor_selection_object_kind_t::bgm_event) {
                        status = "Only notes and BGM events can be prepared for audition.";
                    } else {
                        const auto bindings = audition_sources.bindings();
                        const auto prepared = object_audition.prepare(
                            history.document,
                            {request->document_revision, request->chart, request->object.id},
                            bindings);
                        if (prepared) {
                            if (preview_session.active()) {
                                preview_resume_frame = preview_session.timeline_frame();
                                static_cast<void>(preview_output.stop());
                                preview_paused = true;
                            }
                            const auto started = audition_output.start(prepared.prepared->clips);
                            status = started                   ? "Audio preview started."
                                     : started.message.empty() ? "Audio preview could not start."
                                                               : started.message;
                        } else if (!prepared.diagnostics.empty()) {
                            status = prepared.diagnostics.front().message;
                        } else if (prepared.status ==
                                   wormedit::audio::object_audition_status_t::stale_document) {
                            status = "Audio preview request expired after the document changed.";
                        } else {
                            status = "Audio preview could not be prepared.";
                        }
                    }
#else
                    status = "Audio preview is unavailable in this build.";
#endif
                }
            }
            if (workspace.active_tab != wormedit::workspace_tab_t::compose) {
                compose_viewport_input.escape();
                compose_scene_cache.viewport_autoscroll_active = false;
            }
            if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                compose_interaction_state &&
                workspace.active_tab == wormedit::workspace_tab_t::compose) {
                const auto repeated = dispatch_compose_navigation_actions(
                    compose_navigation_input.advance(static_cast<std::uint64_t>(SDL_GetTicks())),
                    history, *compose_interaction_state);
                if (!repeated.empty()) {
                    status = repeated;
                }
            } else if (workspace.active_tab != wormedit::workspace_tab_t::compose) {
                compose_navigation_input.cancel();
            }
            if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
                running = false;
            } else if (shortcut_handled) {
                // Registry commands take precedence over widget actions emitted
                // by the same input frame, preventing duplicate mutations.
            } else if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
                status = dispatch_audio_offset_drag_action(
                    action, history, workspace, application_command_state.timing_locked,
                    selected_waveform_sample_rate, audio_offset_drag, audio_offset_drag_tab);
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE) {
                status = application_command_state.transport_play_pause
                             ? application_command_state.transport_play_pause()
                             : "Chart preview is unavailable.";
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP) {
                if (application_command_state.transport_stop) {
                    application_command_state.transport_stop();
                    status = "Audio preview stopped.";
                } else {
                    status = "Audio preview is unavailable.";
                }
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK) {
                status = application_command_state.transport_seek
                             ? application_command_state.transport_seek(action.transport_seek_frame)
                             : "Chart preview seek is unavailable.";
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET) {
                const auto applied = wormedit::apply_workspace_action(
                    workspace, history.document, history.document.revision,
                    wormedit::workspace_select_audio_asset_action_t{
                        wormedit::asset_id_t{action.audio_asset_id}});
                status = applied
                             ? "Audio asset selected; waveform analysis scheduled."
                             : (applied.diagnostics.empty() ? "Audio asset selection rejected."
                                                            : applied.diagnostics.front().message);
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
                const auto applied = wormedit::apply_workspace_action(
                    workspace, history.document, history.document.revision,
                    wormedit::workspace_switch_tab_action_t{
                        static_cast<wormedit::workspace_tab_t>(action.tab)});
                status = applied
                             ? "Workspace tab changed"
                             : (applied.diagnostics.empty() ? "Workspace action rejected"
                                                            : applied.diagnostics.front().message);
            } else if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
                status = dispatch_metadata_ui_action(action, history, workspace,
                                                     application_command_state.timing_locked);
            } else if (action.timing_meter_action != WORM_UI_TIMING_METER_ACTION_NONE) {
                const bool binding_matches =
                    action.timing_meter_action == WORM_UI_TIMING_METER_ACTION_APPLY &&
                    workspace.active_tab == wormedit::workspace_tab_t::timing &&
                    workspace.selected_chart &&
                    workspace.selected_chart->value == action.timing_meter_chart_id &&
                    history.document.revision == action.timing_meter_document_revision;
                const auto measure_number = parse_timing_unsigned(action.timing_meter_measure_text);
                const auto numerator = parse_timing_unsigned(action.timing_meter_numerator_text);
                const auto denominator =
                    parse_timing_unsigned(action.timing_meter_denominator_text);
                const auto chart =
                    workspace.selected_chart
                        ? std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id)
                        : history.document.charts.end();
                if (!binding_matches || application_command_state.timing_locked || timing_stage ||
                    timing_recipe_draft || timing_recipe_selection_binding || !measure_number ||
                    *measure_number == 0 || !numerator || !denominator || *numerator == 0 ||
                    *denominator == 0 || *numerator > std::numeric_limits<std::uint32_t>::max() ||
                    *denominator > std::numeric_limits<std::uint32_t>::max() ||
                    chart == history.document.charts.end() ||
                    *measure_number > chart->measures.size()) {
                    status = "Meter change needs a current Timing chart, measure number, and "
                             "bounded signature.";
                } else {
                    const auto& target =
                        chart->measures[static_cast<std::size_t>(*measure_number - 1)];
                    const auto result = wormedit::apply_measure_edit(
                        history, action.timing_meter_document_revision,
                        wormedit::update_measure_definition_action_t{
                            .chart = chart->id,
                            .measure = target.id,
                            .definition = {.length = {*numerator, *denominator},
                                           .meter = wormedit::meter_t{
                                               static_cast<std::uint32_t>(*numerator),
                                               static_cast<std::uint32_t>(*denominator)}}});
                    status = result                       ? "Meter updated."
                             : result.diagnostics.empty() ? "Meter was unchanged."
                                                          : result.diagnostics.front().message;
                    if (result) {
                        const auto rebound = wormedit::rebind_workspace(workspace, history);
                        if (!rebound) {
                            status = rebound.diagnostics.empty()
                                         ? "Meter updated, but workspace rebinding failed."
                                         : rebound.diagnostics.front().message;
                        }
                    }
                }
            } else if (action.timing_grid_action != WORM_UI_TIMING_GRID_ACTION_NONE) {
                const bool binding_matches =
                    action.timing_grid_action == WORM_UI_TIMING_GRID_ACTION_SELECT &&
                    workspace.active_tab == wormedit::workspace_tab_t::timing &&
                    workspace.selected_chart &&
                    workspace.selected_chart->value == action.timing_grid_chart_id &&
                    history.document.revision == action.timing_grid_document_revision &&
                    timing_grid.chart.value == action.timing_grid_chart_id;
                const auto supported = std::ranges::find(timing_grid.supported_denominators,
                                                         action.timing_grid_denominator);
                if (!binding_matches || timing_stage || timing_recipe_draft ||
                    timing_recipe_selection_binding ||
                    supported == timing_grid.supported_denominators.end()) {
                    status = "Timing grid selection expired or cannot change during a staged edit.";
                } else {
                    remember_snap_denominator(application_command_state,
                                              wormedit::chart_id_t{action.timing_grid_chart_id},
                                              action.timing_grid_denominator);
                    const auto chart =
                        std::ranges::find(history.document.charts, *workspace.selected_chart,
                                          &wormedit::chart_document_t::id);
                    if (chart == history.document.charts.end()) {
                        status = "The selected Timing chart is no longer available.";
                    } else {
                        timing_grid = refresh_timing_grid(*chart, application_command_state);
                        status = "Timing grid set to 1/" +
                                 std::to_string(timing_grid.active_denominator) + ".";
                    }
                }
            } else if (action.timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                const bool starting_recipe =
                    action.timing_recipe_action.kind ==
                        WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION ||
                    action.timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN;
                if (starting_recipe && timing_stage) {
                    status = "Apply or cancel the timing event before adding an SV recipe.";
                } else {
                    (void)dispatch_timing_recipe_action(
                        action.timing_recipe_action, history, workspace,
                        application_command_state.timing_locked, timing_grid.active_denominator,
                        timing_recipe_selection_binding, timing_recipe_draft, timing_recipe_preview,
                        status);
                }
            } else if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
                using status_t = wormedit::timing_lane_pseudonote_status_t;
                if (action.timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) {
                    if (timing_recipe_draft || timing_recipe_selection_binding) {
                        status = "Apply or cancel the SV recipe before editing a timing event.";
                    } else if (!timing_stage) {
                        auto candidate = begin_timing_stage(action, history, workspace,
                                                            application_command_state.timing_locked,
                                                            timing_grid.active_denominator);
                        if (candidate.status == status_t::editing) {
                            timing_stage = std::move(candidate);
                            status = timing_stage->existing_event.has_value()
                                         ? "Timing event loaded; apply to update it."
                                         : "Timing event staged; confirm to insert it.";
                        } else if (!candidate.diagnostics.empty()) {
                            status = candidate.diagnostics.front().message;
                        } else {
                            status = "Timing event could not be staged at that row.";
                        }
                    }
                } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL) {
                    if (timing_stage) {
                        wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                        timing_stage.reset();
                        status = "Timing event staging cancelled.";
                    }
                } else if (timing_stage) {
                    const bool stage_matches =
                        workspace.active_tab == wormedit::workspace_tab_t::timing &&
                        workspace.selected_chart &&
                        workspace.selected_chart->value == action.timing_chart_id &&
                        timing_stage->chart.value == action.timing_chart_id &&
                        (timing_stage->existing_event.has_value()
                             ? timing_stage->existing_event->value
                             : std::uint64_t{}) == action.timing_object_id &&
                        timing_stage->document_revision == action.timing_document_revision &&
                        history.document.revision == action.timing_document_revision;
                    if (!stage_matches) {
                        wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                        timing_stage.reset();
                        status =
                            "Timing event staging expired because its chart or revision changed.";
                    } else {
                        const auto chart =
                            std::ranges::find(history.document.charts, timing_stage->chart,
                                              &wormedit::chart_document_t::id);
                        if (action.timing_stage_action == WORM_UI_TIMING_STAGE_DELETE) {
                            const auto kind = timing_lane_kind_from_ui(action.timing_lane);
                            if (!timing_stage->existing_event || !kind ||
                                *kind != timing_stage->kind) {
                                status = "The selected timing event cannot be deleted.";
                            } else {
                                const auto result = wormedit::delete_existing_timing_lane_event(
                                    history, timing_stage->chart, *kind,
                                    *timing_stage->existing_event, action.timing_document_revision,
                                    application_command_state.timing_locked);
                                status = result ? "Timing event deleted."
                                         : result.diagnostics.empty()
                                             ? "Timing event deletion was rejected."
                                             : result.diagnostics.front().message;
                            }
                            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                            timing_stage.reset();
                        } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_MOVE) {
                            const auto target =
                                chart == history.document.charts.end() ||
                                        !timing_stage->existing_event || !action.timing_has_target_y
                                    ? std::optional<wormedit::local_position_t>{}
                                    : timing_position_from_fraction(*chart,
                                                                    action.timing_target_y_fraction,
                                                                    timing_grid.active_denominator);
                            if (!target || !wormedit::move_existing_timing_lane_pseudonote(
                                               history, *timing_stage, *target,
                                               application_command_state.timing_locked)) {
                                status = timing_stage->diagnostics.empty()
                                             ? "The timing marker cannot move to that row."
                                             : timing_stage->diagnostics.front().message;
                            } else {
                                status =
                                    "Timing marker moved in the staged preview; apply to commit.";
                            }
                        } else {
                            const auto value =
                                chart == history.document.charts.end()
                                    ? std::optional<wormedit::timing_lane_value_t>{}
                                    : timing_stage_value(*timing_stage, *chart, action,
                                                         timing_grid.active_denominator);
                            if (!value) {
                                timing_stage->valid = false;
                                timing_stage->diagnostics = {{
                                    .severity = wormedit::diagnostic_severity_t::error,
                                    .code = wormedit::diagnostic_code_t::invalid_rational,
                                    .message =
                                        "Enter a bounded exact rational for this timing value.",
                                }};
                                status = "Timing value is outside its supported bounds.";
                            } else {
                                const bool valid = wormedit::update_timing_lane_pseudonote(
                                    history, *timing_stage, *value,
                                    application_command_state.timing_locked);
                                if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM &&
                                    valid) {
                                    const auto result = wormedit::confirm_timing_lane_pseudonote(
                                        history, *timing_stage,
                                        application_command_state.timing_locked);
                                    const auto edited_existing =
                                        timing_stage->existing_event.has_value();
                                    status = result ? (edited_existing ? "Timing event updated."
                                                                       : "Timing event inserted.")
                                             : result.diagnostics.empty() && edited_existing
                                                 ? "Timing event was unchanged."
                                             : result.diagnostics.empty()
                                                 ? "Timing event insertion was rejected."
                                                 : result.diagnostics.front().message;
                                    timing_stage.reset();
                                } else if (action.timing_stage_action ==
                                           WORM_UI_TIMING_STAGE_CONFIRM) {
                                    status = timing_stage->diagnostics.empty()
                                                 ? "Timing event values are invalid."
                                                 : timing_stage->diagnostics.front().message;
                                }
                            }
                        }
                    }
                }
            } else if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
                if (compose_interaction_state) {
                    status = dispatch_compose_view_action(
                        action, history, workspace, *compose_interaction_state,
                        flattened.view.compose.viewport_height_pixels, application_command_state);
                }
            } else if (action.kind >= WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT &&
                       action.kind <= WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) {
                if (autosave_loop.busy()) {
                    status = "Wait for the current autosave to finish before recovering a session.";
                } else {
                    status =
                        dispatch_recovery_ui_action(action, recovery_controller, history, workspace,
                                                    scanned_primary_version, autosave_loop);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                    if (const auto unavailable = reconcile_audition_sources(true);
                        unavailable && *unavailable != 0) {
                        status += " " + std::to_string(*unavailable) +
                                  " recovered audio asset(s) are unavailable for preview.";
                    }
#endif
                }
            }
        }

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_ui_frontend_render(width, height);
        worm_window_swap(&window);
        SDL_Delay(1);
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    preview_output.close();
    audition_output.close();
#endif
    worm_ui_frontend_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}

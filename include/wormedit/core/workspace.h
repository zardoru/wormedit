#pragma once

#include <wormedit/core/vocabulary.h>

#include <wormedit/core/command.h>
#include <wormedit/core/catalogs.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {


// UI-independent tabs for the song workspace. Storyboard is intentionally
// present in the navigation model while its editor is not implemented yet.

inline constexpr std::size_t workspace_tab_count = 7;

struct workspace_tab_descriptor_t {
    workspace_tab_t kind;
    std::string_view id;
    std::string_view label;
};
inline constexpr std::array workspace_tabs{
    workspace_tab_descriptor_t{workspace_tab_t::setup, "setup", "Project"},
    workspace_tab_descriptor_t{workspace_tab_t::metadata, "metadata", "Metadata"},
    workspace_tab_descriptor_t{workspace_tab_t::timing, "timing", "Timing"},
    workspace_tab_descriptor_t{workspace_tab_t::compose, "compose", "Compose"},
    workspace_tab_descriptor_t{workspace_tab_t::storyboard, "storyboard", "Storyboard"},
    workspace_tab_descriptor_t{workspace_tab_t::audio_slices, "audio_slices", "Audio Slices"},
    workspace_tab_descriptor_t{workspace_tab_t::compare_validate, "compare_validate", "Compare / Validate"},
};
static_assert(workspace_tab_count == static_cast<std::size_t>(workspace_tab_t::compare_validate) + 1);
static_assert(is_catalog_exhaustive(workspace_tabs, workspace_tab_count));

[[nodiscard]] auto is_workspace_tab_enabled(workspace_tab_t tab) noexcept -> bool;

struct workspace_limits_t {
    std::size_t maximum_charts{1'024};
    std::size_t maximum_audio_assets{4'096};
    std::size_t maximum_visual_assets{4'096};
    std::size_t maximum_midi_assets{4'096};
    std::size_t maximum_resources{100'000};
    std::size_t maximum_storyboards{1'024};
    std::size_t maximum_comparison_charts{8};
    std::size_t maximum_summary_items{1'000'000};
    std::size_t maximum_focus_region_bytes{128};
    std::size_t maximum_validation_diagnostics{4'096};
    std::size_t maximum_metadata_text_bytes{32U * 1024U * 1024U};
    std::size_t maximum_bms_branch_rows{16'384};
    std::size_t maximum_bms_branch_depth{512};
    std::size_t maximum_bms_branch_text_bytes{1'024};
    std::size_t maximum_bms_branch_display_bytes{4U * 1024U * 1024U};
    auto operator==(const workspace_limits_t&) const -> bool = default;
};

// Focus is logical rather than widget/toolkit-specific. A focus target can be
// an identified document item, while region names let a backend restore the
// correct editor control independently for each tab.
using workspace_focus_target_t = std::variant<std::monostate, chart_id_t, object_id_t, asset_id_t,
                                              clip_id_t, resource_id_t, storyboard_id_t>;

struct workspace_tab_focus_t {
    std::string region;
    workspace_focus_target_t target;
};

struct workspace_transport_t {
    std::optional<asset_id_t> audio_asset;
    std::optional<clip_id_t> selected_clip;
    std::uint64_t position_frame{};
    bool playing{};
    std::optional<std::pair<std::uint64_t, std::uint64_t>> loop_frames;

    auto operator==(const workspace_transport_t&) const -> bool = default;
};

struct workspace_history_summary_t {
    bool available{};
    std::size_t undo_depth{};
    std::size_t redo_depth{};
    bool dirty{};

    auto operator==(const workspace_history_summary_t&) const -> bool = default;
};

struct workspace_controller_t {
    [[nodiscard]] static auto from(const song_document_t& document,
                                   const workspace_limits_t& limits = {})
        -> diagnosed_t<std::optional<workspace_controller_t>>;
    [[nodiscard]] static auto from(const document_history_t& history,
                                   const workspace_limits_t& limits = {})
        -> diagnosed_t<std::optional<workspace_controller_t>>;

    song_id_t song;
    std::uint64_t document_revision{};
    workspace_tab_t active_tab{workspace_tab_t::setup};
    std::optional<chart_id_t> selected_chart;
    std::optional<asset_id_t> selected_visual_asset;
    std::vector<chart_id_t> comparison_charts;
    workspace_transport_t transport;
    workspace_history_summary_t history;
    std::array<workspace_tab_focus_t, workspace_tab_count> tab_focus;
    workspace_limits_t limits;
};

using workspace_bind_outcome_t = diagnosed_t<std::optional<workspace_controller_t>>;

struct workspace_switch_tab_action_t {
    workspace_tab_t tab{workspace_tab_t::setup};
};
struct workspace_select_chart_action_t {
    chart_id_t chart;
};
struct workspace_set_comparison_charts_action_t {
    std::vector<chart_id_t> charts;
};
struct workspace_set_focus_action_t {
    workspace_tab_t tab{workspace_tab_t::setup};
    std::string region;
    workspace_focus_target_t target;
};
struct workspace_clear_focus_action_t {
    workspace_tab_t tab{workspace_tab_t::setup};
};
enum class workspace_asset_slot_t { audio, visual };

struct workspace_select_asset_action_t {
    std::optional<asset_id_t> asset;
    workspace_asset_slot_t slot{workspace_asset_slot_t::audio};
};
struct workspace_select_clip_action_t {
    std::optional<clip_id_t> clip;
};
struct workspace_seek_action_t {
    std::uint64_t frame{};
};
struct workspace_set_playing_action_t {
    bool playing{};
};
struct workspace_set_loop_action_t {
    std::optional<std::pair<std::uint64_t, std::uint64_t>> frames;
};

using workspace_action_t =
    std::variant<workspace_switch_tab_action_t, workspace_select_chart_action_t,
                 workspace_set_comparison_charts_action_t, workspace_set_focus_action_t,
                 workspace_clear_focus_action_t, workspace_select_asset_action_t,
                 workspace_select_clip_action_t, workspace_seek_action_t,
                 workspace_set_playing_action_t, workspace_set_loop_action_t>;

// Actions are rejected atomically unless the bound document identity and
// revision still match. Rebind after publishing/undoing an
// edit; matching stable selections and transport context are retained.
[[nodiscard]] auto
apply_workspace_action(workspace_controller_t& workspace, const song_document_t& document,
                       const workspace_action_t& action)
    -> dispatch_outcome_t;
[[nodiscard]] auto rebind_workspace(workspace_controller_t& workspace,
                                    const song_document_t& document) -> dispatch_outcome_t;
[[nodiscard]] auto rebind_workspace(workspace_controller_t& workspace,
                                    const document_history_t& history) -> dispatch_outcome_t;

struct workspace_chart_option_t {
    [[nodiscard]] static auto from(const chart_document_t& chart) -> workspace_chart_option_t;

    chart_id_t id;
    std::string difficulty_name;
    std::optional<signed_rational_t> rating;
};

struct workspace_setup_view_t {
    std::string title;
    std::string artist;
    std::vector<workspace_chart_option_t> charts;
    std::size_t audio_asset_count{};
    std::size_t visual_asset_count{};
};

struct workspace_metadata_view_t {
    struct format_field_t {
        bool chart_scoped{};
        format_family_t family{format_family_t::native_project};
        bool first_group_for_family{};
        std::size_t group_index{};
        std::size_t field_index{};
        std::size_t group_field_count{};
        std::string name;
        std::string value;
        std::optional<std::size_t> source_order;
    };

    std::string title;
    std::string artist;
    std::vector<std::string> search_tags;
    std::optional<workspace_chart_option_t> selected_difficulty;
    std::string chart_author;
    signed_rational_t beat_zero_audio_offset_seconds;
    std::vector<format_field_t> format_fields;
};

struct workspace_timing_view_t {
    chart_id_t chart;
    std::size_t measure_count{};
    std::size_t bpm_count{};
    std::size_t pause_count{};
    std::size_t warp_count{};
    std::size_t bookmark_count{};
    std::size_t section_count{};
    std::size_t anchor_count{};
};

struct workspace_bms_branch_row_t {
    bms_control_kind_t kind{bms_control_kind_t::raw_statement};
    branch_id_t id;
    std::size_t depth{};
    std::optional<std::size_t> source_order;
    bool active{};
    bool selected{};
    std::string display_text;
    bool display_text_truncated{};
};

struct workspace_bms_branch_choice_t {
    branch_id_t id;
    bms_control_kind_t kind{bms_control_kind_t::random};
    std::optional<std::size_t> source_order;
    bool active{};
    bool editable{};
    std::optional<std::int64_t> selected_value;
    std::optional<std::int64_t> previous_value;
    std::optional<std::int64_t> next_value;
    bool can_clear{};
};

struct workspace_bms_branch_outline_cache_key_t;
struct workspace_bms_branch_outline_view_t;
// Shared so a cached outline reaches each frame's view without copying rows and choices.
using workspace_shared_bms_branch_outline_t =
    std::shared_ptr<const workspace_bms_branch_outline_view_t>;
using workspace_bms_branch_outline_cache_t = keyed_cache_t<
    workspace_bms_branch_outline_cache_key_t, workspace_shared_bms_branch_outline_t>;

struct workspace_bms_branch_outline_view_t {
    // Null when the chart has no BMS control tree.
    [[nodiscard]] static auto from(const song_document_t& document, const chart_document_t& chart,
                                   const workspace_limits_t& limits,
                                   workspace_bms_branch_outline_cache_t* cache)
        -> workspace_shared_bms_branch_outline_t;

    [[nodiscard]] static auto from(const chart_document_t& chart, const workspace_limits_t& limits)
        -> workspace_bms_branch_outline_view_t;

    bool available{};
    bool rows_truncated{};
    bool choices_truncated{};
    bool display_text_truncated{};
    bool diagnostics_truncated{};
    std::vector<workspace_bms_branch_row_t> rows;
    std::vector<workspace_bms_branch_choice_t> choices;
    diagnostic_list_t diagnostics;
};

struct workspace_document_cache_query_t {
    const song_document_t& document;
    const workspace_limits_t& limits;
};

struct workspace_document_cache_key_t {
    const song_document_t* document_identity{};
    document_stamp_t stamp;
    workspace_limits_t limits;

    [[nodiscard]] static auto from(const workspace_document_cache_query_t& query)
        -> workspace_document_cache_key_t;
    void assign(const workspace_document_cache_query_t& query);
    [[nodiscard]] auto operator==(const workspace_document_cache_query_t& query) const -> bool;
    auto operator==(const workspace_document_cache_key_t&) const -> bool = default;
};

// Document mutations advance revision. Per-frame focus/chart/tab guards remain uncached.
using workspace_binding_validation_cache_t =
    keyed_cache_t<workspace_document_cache_key_t, diagnostic_list_t>;

struct workspace_bms_branch_outline_cache_query_t {
    workspace_document_cache_query_t document;
    const chart_document_t& chart;
};

// Choice ownership is captured only on misses; repeated fills reuse its storage.
struct workspace_bms_branch_outline_cache_key_t {
    workspace_document_cache_key_t document;
    const chart_document_t* chart_identity{};
    chart_id_t chart;
    std::vector<branch_selection_t> choices;

    [[nodiscard]] static auto from(const workspace_bms_branch_outline_cache_query_t& query)
        -> workspace_bms_branch_outline_cache_key_t;
    void assign(const workspace_bms_branch_outline_cache_query_t& query);
    [[nodiscard]] auto operator==(const workspace_bms_branch_outline_cache_query_t& query) const
        -> bool;
    auto operator==(const workspace_bms_branch_outline_cache_key_t&) const -> bool = default;
};

struct workspace_compose_view_t {
    chart_id_t chart;
    std::size_t measure_count{};
    std::size_t note_count{};
    std::size_t layout_segment_count{};
    std::size_t lane_count{};
    std::size_t layer_count{};
    workspace_shared_bms_branch_outline_t shared_bms_branch_outline;

    // An absent outline reads as an empty, unavailable one.
    [[nodiscard]] auto bms_branch_outline() const -> const workspace_bms_branch_outline_view_t&;
};

struct workspace_storyboard_view_t {
    bool enabled{};
    std::string placeholder;
    std::size_t storyboard_count{};
};

struct workspace_audio_slices_view_t {
    std::vector<asset_id_t> audio_assets;
    std::size_t clip_count{};
    std::size_t sound_channel_count{};
    workspace_transport_t transport;
};

struct workspace_compare_validate_view_t {
    std::vector<chart_id_t> comparison_charts;
    std::size_t error_count{};
    std::size_t warning_count{};
    diagnostic_list_t diagnostics;
};

using workspace_tab_view_t =
    std::variant<workspace_setup_view_t, workspace_metadata_view_t, workspace_timing_view_t,
                 workspace_compose_view_t, workspace_storyboard_view_t,
                 workspace_audio_slices_view_t, workspace_compare_validate_view_t>;

struct workspace_view_t {
    [[nodiscard]] static auto from(const workspace_controller_t& workspace,
                                   const song_document_t& document) -> diagnosed_t<std::optional<workspace_view_t>>;
    // Also builds a non-active tab's view. This lets the shell display the
    // storyboard placeholder while navigation/actions continue to reject it.
    // Compare diagnostics may be supplied from a cache keyed to this document's
    // ID and revision; a null pointer preserves the normal validation behavior.
    [[nodiscard]] static auto from(const workspace_controller_t& workspace,
                                   const song_document_t& document, workspace_tab_t tab,
                                   const diagnostic_list_t* cached_diagnostics = nullptr,
                                   workspace_bms_branch_outline_cache_t* branch_cache = nullptr,
                                   workspace_binding_validation_cache_t* binding_cache = nullptr,
                                   std::optional<branch_id_t> authoring_chart = {})
        -> diagnosed_t<std::optional<workspace_view_t>>;

    song_id_t song;
    std::uint64_t document_revision{};
    workspace_tab_t active_tab{workspace_tab_t::setup};
    bool tab_enabled{true};
    std::optional<workspace_chart_option_t> selected_difficulty;
    workspace_transport_t transport;
    std::optional<asset_id_t> selected_visual_asset;
    workspace_history_summary_t history;
    workspace_tab_focus_t focus;
    workspace_tab_view_t tab_view;
};

using workspace_view_outcome_t = diagnosed_t<std::optional<workspace_view_t>>;

} // namespace wormedit

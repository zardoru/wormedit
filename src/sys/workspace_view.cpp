#include <wormedit/core/bmson_lane_mode.h>
#include "sys/workspace_view.h"
#include "sys/catalog_view.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "sys/compose_resource_presenter.h"
#include "sys/sound_list_view.h"
#include "sys/sound_usage_view.h"
#include "sys/bookmark_navigation.h"
#include "sys/sound_picker_view.h"
#include "wormedit/config.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
auto flattened_workspace_t::arena() noexcept -> frame_arena_t* {
    if (arena_) return arena_;
    if (!owned_arena_) {
        try {
            owned_arena_ = std::make_shared<frame_arena_t>(arena_options_t{
                .chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    }
    return owned_arena_.get();
}


auto arena_string(frame_arena_t& arena, std::string_view value) noexcept -> const char* {
    if (value.size() == std::numeric_limits<std::size_t>::max()) return nullptr;
    const auto allocation = arena.allocate(value.size() + 1, alignof(char));
    if (!allocation) return nullptr;
    auto* text = static_cast<char*>(*allocation);
    if (!value.empty()) std::memcpy(text, value.data(), value.size());
    text[value.size()] = '\0';
    return text;
}

auto arena_format(frame_arena_t& arena, const char* format, ...) noexcept -> const char* {
    if (!format) return nullptr;
    std::va_list arguments;
    va_start(arguments, format);
    std::va_list sizing;
    va_copy(sizing, arguments);
    const auto length = std::vsnprintf(nullptr, 0, format, sizing);
    va_end(sizing);
    if (length < 0) {
        va_end(arguments);
        return nullptr;
    }
    const auto allocation = arena.allocate(static_cast<std::size_t>(length) + 1, alignof(char));
    if (!allocation) {
        va_end(arguments);
        return nullptr;
    }
    auto* text = static_cast<char*>(*allocation);
    const auto written = std::vsnprintf(text, static_cast<std::size_t>(length) + 1,
                                         format, arguments);
    va_end(arguments);
    return written == length ? text : nullptr;
}
namespace {

static_assert(static_cast<unsigned>(workspace_tab_t::setup) == WORM_UI_WORKSPACE_TAB_SETUP);
static_assert(static_cast<unsigned>(workspace_tab_t::metadata) == WORM_UI_WORKSPACE_TAB_METADATA);
static_assert(static_cast<unsigned>(workspace_tab_t::timing) == WORM_UI_WORKSPACE_TAB_TIMING);
static_assert(static_cast<unsigned>(workspace_tab_t::compose) == WORM_UI_WORKSPACE_TAB_COMPOSE);
static_assert(static_cast<unsigned>(workspace_tab_t::storyboard) == WORM_UI_WORKSPACE_TAB_STORYBOARD);
static_assert(static_cast<unsigned>(workspace_tab_t::audio_slices) == WORM_UI_WORKSPACE_TAB_AUDIO_SLICES);
static_assert(static_cast<unsigned>(workspace_tab_t::compare_validate) == WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE);

auto ui_stepmania_file_profile(wormedit::stepmania_profile_t profile)
    -> WormUiStepmaniaFileProfile {
    return profile == wormedit::stepmania_profile_t::sm ? WORM_UI_STEPMANIA_FILE_SM
                                                        : WORM_UI_STEPMANIA_FILE_SSC;
}

auto ui_stepmania_timing_syntax(wormedit::stepmania_timing_syntax_t syntax)
    -> WormUiStepmaniaTimingSyntax {
    switch (syntax) {
    case wormedit::stepmania_timing_syntax_t::native_warps:
        return WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
    case wormedit::stepmania_timing_syntax_t::legacy_negative_bpm:
        return WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM;
    case wormedit::stepmania_timing_syntax_t::legacy_negative_stop:
        return WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP;
    case wormedit::stepmania_timing_syntax_t::unspecified:
        return WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
    }
    return WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
}

auto ui_stepmania_implementation(wormedit::stepmania_implementation_profile_t implementation)
    -> WormUiStepmaniaImplementation {
    switch (implementation) {
    case wormedit::stepmania_implementation_profile_t::unspecified:
        return WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY;
    case wormedit::stepmania_implementation_profile_t::stepmania_5_1:
        return WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1;
    case wormedit::stepmania_implementation_profile_t::etterna_0_75_1:
        return WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1;
    }
    return WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY;
}

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

auto recovery_number_suffix(const char* label, std::optional<std::uint64_t> value)
    -> std::array<char, 64> {
    std::array<char, 64> suffix{};
    if (value) std::snprintf(suffix.data(), suffix.size(), " · %s %llu", label,
                             static_cast<unsigned long long>(*value));
    return suffix;
}

auto recovery_summary(frame_arena_t& arena,
                      const wormedit::native_project_recovery_banner_row_t& row,
                      bool recommended) -> const char* {
    const auto revision = recovery_number_suffix("revision", row.version
        ? std::optional<std::uint64_t>{row.version->revision} : std::nullopt);
    const auto session = recovery_number_suffix("session", row.recovery_session_sequence);
    return arena_format(arena, "%s · %s%s%s%s", recovery_status_name(row.status).data(),
        recovery_comparison_name(row.comparison).data(), revision.data(), session.data(),
        recommended ? " · recommended recovery" : "");
}

auto populate_recovery_candidate(
    flattened_workspace_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery,
    std::size_t source_index, std::size_t index) -> bool {
    const auto& row = recovery.rows[source_index];
    auto* arena = result.arena();
    const auto* path = arena ? arena_string(*arena, row.path_label) : nullptr;
    const auto* summary = arena ? recovery_summary(*arena, row,
        recovery.preferred_recovery_row == source_index) : nullptr;
    if (!path || !summary) {
        result.view.recovery_rows_truncated = true;
        result.view.recovery_notice = "Recovery display text truncated (frame storage limit).";
        return false;
    }
    const auto action_mask = recovery_action_mask(row.available_actions);
    constexpr auto all_recovery_actions =
        WORM_UI_RECOVERY_ACTION_INSPECT | WORM_UI_RECOVERY_ACTION_RECOVER |
        WORM_UI_RECOVERY_ACTION_SAVE_AS | WORM_UI_RECOVERY_ACTION_DISCARD |
        WORM_UI_RECOVERY_ACTION_QUARANTINE;
    const char* disabled_reason = nullptr;
    if (row.available_actions.empty()) {
        disabled_reason = "No safe action is available for this candidate.";
    } else if (action_mask != all_recovery_actions) {
        disabled_reason =
            "Unavailable actions are disabled by the recovery controller's safety checks.";
    }
    result.view.recovery_candidates[index] = {
        .path_label = path, .summary = summary, .disabled_reason = disabled_reason,
        .action_mask = action_mask,
        .scan_generation = row.target.scan_generation,
        .row_index = row.target.row_index,
        .candidate_index = row.target.candidate_index,
    };
    return true;
}

void populate_bounded_recovery_candidates(
    flattened_workspace_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery) {
    const auto capacity = result.view.recovery_candidate_count;
    result.view.recovery_candidate_count = 0;
    // Keep actionable rows ahead of inactive rows without changing action tokens.
    for (const bool actionable : {true, false}) {
        for (std::size_t source_index = 0; source_index < recovery.rows.size(); ++source_index) {
            if (result.view.recovery_candidate_count == capacity) return;
            const bool row_actionable = !recovery.rows[source_index].available_actions.empty();
            if (row_actionable != actionable) continue;
            if (!populate_recovery_candidate(result, recovery, source_index,
                                              result.view.recovery_candidate_count)) return;
            ++result.view.recovery_candidate_count;
        }
    }
}

void populate_recovery_view(
    flattened_workspace_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery,
    bool recovery_scan_configured) {
    result.view.recovery_has_actionable_candidate =
        std::any_of(recovery.rows.begin(), recovery.rows.end(), [](const auto& row) {
            return !row.available_actions.empty();
        });
    result.view.recovery_panel_visible = result.view.recovery_has_actionable_candidate;
    result.view.recovery_heading = "Project recovery";
    result.view.recovery_rows_truncated = recovery.rows_truncated;
    if (recovery_scan_configured) {
        result.view.recovery_notice = result.view.recovery_has_actionable_candidate
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
    populate_bounded_recovery_candidates(result, recovery);
    const auto recovery_diagnostic_count =
        std::min(recovery.diagnostics.size(),
                 static_cast<std::size_t>(WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY));
    auto* arena = result.arena();
    for (std::size_t index = 0; index < recovery_diagnostic_count; ++index) {
        const auto& diagnostic = recovery.diagnostics[index];
        const auto* text = arena ? (diagnostic.path.empty()
            ? arena_string(*arena, diagnostic.message)
            : arena_format(*arena, "%s (%s)", diagnostic.message.c_str(),
                           diagnostic.path.generic_string().c_str())) : nullptr;
        result.view.recovery_diagnostics[result.view.recovery_diagnostic_count++] = text
            ? text : "Recovery display text truncated (frame storage limit).";
        if (!text) {
            result.view.recovery_rows_truncated = true;
            break;
        }
    }
}

struct workspace_projection_context_t {
    flattened_workspace_t& result;
    const wormedit::workspace_view_t& source;
    const wormedit::song_document_t& document;
    const wormedit::stepmania_export_policy_t& stepmania_policy;
    dock_sort_t keysound_sort;
    WormUiKeysoundDockRequest keysound_request;
    WormUiSoundUsageRequest sound_usage_request;
    WormUiBookmarkChooserRequest bookmark_chooser_request;
    keysound_list_cache_t* keysound_cache{};
};

void publish_workspace_summary(flattened_workspace_t& result, const char* text) {
    if (result.summary.truncated || result.view.summary_line_count >= result.summary.lines.size())
        return;
    if (!text) {
        text = "Workspace summary truncated (frame storage limit).";
        result.summary.truncated = true;
    }
    const auto index = result.view.summary_line_count++;
    result.summary.lines[index] = text;
    result.view.summary_lines[index] = text;
}
void append_workspace_summary(const workspace_projection_context_t& projection,
                              std::string_view line) {
    const auto& result = projection.result;
    if (result.summary.truncated || result.view.summary_line_count >= result.summary.lines.size())
        return;
    auto* arena = projection.result.arena();
    publish_workspace_summary(projection.result, arena ? arena_string(*arena, line) : nullptr);
}
template<class... Args>
void append_workspace_format(const workspace_projection_context_t& projection,
                             const char* format, Args... arguments) {
    const auto& result = projection.result;
    if (result.summary.truncated || result.view.summary_line_count >= result.summary.lines.size())
        return;
    auto* arena = projection.result.arena();
    publish_workspace_summary(projection.result,
        arena ? arena_format(*arena, format, arguments...) : nullptr);
}

void populate_setup_tab(const workspace_projection_context_t& projection, const wormedit::workspace_setup_view_t& tab) {
    auto& result = projection.result;
    const auto& stepmania_policy = projection.stepmania_policy;
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                auto& setup = result.view.setup;
                setup.visible = true;
                setup.chart_count = tab.charts.size();
                setup.audio_asset_count = tab.audio_asset_count;
                setup.visual_asset_count = tab.visual_asset_count;
                setup.stepmania_file_profile =
                    ui_stepmania_file_profile(stepmania_policy.file_profile);
                setup.stepmania_timing_syntax =
                    ui_stepmania_timing_syntax(stepmania_policy.timing_syntax);
                setup.stepmania_implementation =
                    ui_stepmania_implementation(stepmania_policy.implementation_profile);
                const auto resolved = wormedit::stepmania_export_policy_result_t::from(stepmania_policy);
                setup.stepmania_policy_valid = static_cast<bool>(resolved);
                const auto status = resolved
                    ? std::string_view{
                        stepmania_policy.file_profile == wormedit::stepmania_profile_t::sm &&
                                stepmania_policy.timing_syntax ==
                                    wormedit::stepmania_timing_syntax_t::native_warps
                                   ? "Valid syntax policy; SM charts containing warps require a "
                                     "legacy "
                              "encoding instead."
                            : "Compatible StepMania export policy."}
                    : std::string_view{resolved.error().message};
                auto* arena = result.arena();
                const auto* copied = arena ? arena_string(*arena, status) : nullptr;
                result.setup.text_truncated = !copied;
                result.setup.policy_status = copied ? copied :
                    "Setup display text truncated (frame storage limit).";
                setup.stepmania_policy_status = result.setup.policy_status;

}

template<class T>
auto metadata_rows(flattened_workspace_t& result, std::size_t count) -> frame_span<T> {
    auto* arena = result.arena();
    return arena ? arena_array<T>(*arena, count) : frame_span<T>{};
}

void populate_metadata_search_tags(flattened_workspace_t& result,
                                   const wormedit::workspace_metadata_view_t& tab,
                                   WormUiMetadataView& metadata) {
    if (!metadata.search_tags_editable) return;
    if (std::ranges::any_of(tab.search_tags, [](const auto& tag) {
        return tag.size() > WORM_UI_METADATA_SEARCH_TAG_BYTES;
    })) {
        metadata.search_tags_editable = false;
        return;
    }
    result.metadata.search_tag = metadata_rows<const char*>(result, tab.search_tags.size());
    result.metadata.search_tag_length = metadata_rows<std::size_t>(result, tab.search_tags.size());
    auto* arena = result.arena();
    metadata.search_tags_editable = arena &&
        result.metadata.search_tag.capacity() == tab.search_tags.size() &&
        result.metadata.search_tag_length.capacity() == tab.search_tags.size();
    if (!metadata.search_tags_editable) return;
    for (const auto& tag : tab.search_tags) {
        const auto* text = arena_string(*arena, tag);
        if (!text) { metadata.search_tags_editable = false; return; }
        (void)result.metadata.search_tag.push_back(text);
        (void)result.metadata.search_tag_length.push_back(tag.size());
    }
}

void populate_metadata_format_fields(flattened_workspace_t& result,
                                     const wormedit::workspace_metadata_view_t& tab,
                                     WormUiMetadataView& metadata) {
    metadata.format_field_total_count = tab.format_fields.size();
    const auto count = std::min<std::size_t>(tab.format_fields.size(),
                                            WORM_UI_METADATA_FORMAT_FIELD_CAPACITY);
    metadata.format_fields_truncated = count != tab.format_fields.size();
    result.metadata.format_field = metadata_rows<WormUiMetadataFormatFieldView>(result, count);
    auto* arena = result.arena();
    if (!arena || result.metadata.format_field.capacity() != count) {
        metadata.format_fields_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& field = tab.format_fields[index];
        const auto* name = arena_string(*arena, field.name);
        const auto* value = arena_string(*arena, field.value);
        if (!name || !value) { metadata.format_fields_truncated = true; break; }
        (void)result.metadata.format_field.push_back({
            .chart_scoped = field.chart_scoped,
            .family = static_cast<std::uint8_t>(field.family),
            .first_group_for_family = field.first_group_for_family,
            .group_index = field.group_index, .field_index = field.field_index,
            .group_field_count = field.group_field_count,
            .name = name, .value = value,
            .name_bytes = field.name.size(), .value_bytes = field.value.size(),
            .has_source_order = field.source_order.has_value(),
            .source_order = field.source_order.value_or(0),
        });
    }
    metadata.format_field_count = result.metadata.format_field.size();
    metadata.format_fields = result.metadata.format_field.data();
}

void append_metadata_format_targets(flattened_workspace_t& result, auto& metadata,
                                    const auto& groups, bool chart_scoped) {
    std::array<const wormedit::format_metadata_t*, wormedit::format_families.size()> first_groups{};
    std::array<std::size_t, wormedit::format_families.size()> first_group_indices{};
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto family_index = static_cast<std::size_t>(groups[group_index].family);
        if (family_index < first_groups.size() && first_groups[family_index] == nullptr) {
            first_groups[family_index] = &groups[group_index];
            first_group_indices[family_index] = group_index;
        }
    }
    for (const auto& descriptor : wormedit::format_families) {
        if (!descriptor.offered_as_metadata) continue;
        const auto family = descriptor.kind;
        const auto family_index = static_cast<std::size_t>(family);
        const auto* found = first_groups[family_index];
        (void)result.metadata.format_target.push_back({
            .chart_scoped = chart_scoped,
            .family = static_cast<std::uint8_t>(family),
            .group_exists = found != nullptr,
            .group_index = found == nullptr ? 0 : first_group_indices[family_index],
            .field_count = found == nullptr ? 0 : found->ordered_fields.size(),
        });
        metadata.format_target_count = result.metadata.format_target.size();
    }
}

void populate_metadata_format_targets(const workspace_projection_context_t& projection,
                                      const wormedit::workspace_metadata_view_t& tab,
                                      auto& metadata) {
    const auto& document = projection.document;
    auto& result = projection.result;
    const auto selected = tab.selected_difficulty
        ? document.find_chart(tab.selected_difficulty->id) : nullptr;
    if (selected != nullptr)
        metadata.selected_chart_source_family = static_cast<std::uint8_t>(selected->source_family);
    const auto count = metadata_family_view.count * (selected ? 2u : 1u);
    result.metadata.format_target = metadata_rows<WormUiMetadataFormatTargetView>(result, count);
    if (result.metadata.format_target.capacity() != count) {
        metadata.format_fields_truncated = true;
        return;
    }
    append_metadata_format_targets(result, metadata, document.metadata.format_groups, false);
    if (selected == nullptr) return;
    append_metadata_format_targets(result, metadata, selected->metadata.format_groups, true);
}

void populate_metadata_tab(const workspace_projection_context_t& projection, const wormedit::workspace_metadata_view_t& tab) {
    auto& result = projection.result;
    const auto& source = projection.source;
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
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
                populate_metadata_search_tags(result, tab, metadata);
                metadata.search_tags = metadata.search_tags_editable
                                           ? result.metadata.search_tag.data()
                                           : nullptr;
                metadata.search_tag_bytes = metadata.search_tags_editable
                                                ? result.metadata.search_tag_length.data()
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
                populate_metadata_format_fields(result, tab, metadata);
                populate_metadata_format_targets(projection, tab, metadata);
                metadata.format_targets = result.metadata.format_target.data();
                append("Song title: " + tab.title);
                append("Artist: " + tab.artist);
                append("Search tags: " + std::to_string(tab.search_tags.size()));
                append("Chart author: " +
                       (tab.chart_author.empty() ? std::string("(unset)") : tab.chart_author));
                append("Format-specific fields: " + std::to_string(tab.format_fields.size()));
                for (const auto& field : tab.format_fields) {
                    append(std::string(field.chart_scoped ? "Difficulty · " : "Song · ") +
                           std::string(wormedit::format_family_label(field.family)) + " · " + field.name +
                           ": " + metadata_value_summary(field.value));
                }

}
void populate_timing_tab(const workspace_projection_context_t& projection, const wormedit::workspace_timing_view_t& tab) {
    auto& result = projection.result;
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
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

}
auto append_compose_output_group(flattened_workspace_t& result, frame_arena_t& arena,
                                 const wormedit::output_group_t& group,
                                 std::size_t document_index) -> bool {
    const auto* name = arena_string(arena, group.name);
    const auto* path = arena_string(arena, group.relative_output_path.generic_string());
    const auto directory = wormedit::export_asset_directory(group.relative_output_path);
    const auto* assets = arena_string(arena,
        directory ? directory->generic_string() : std::string{});
    if (!name || !path || !assets) return false;
    if (!result.compose.output_group.view.push_back({
        .document_index = document_index, .name = name, .relative_output_path = path,
        .relative_asset_directory = assets})) return false;
    result.view.compose.output_group_count = result.compose.output_group.view.size();
    return true;
}

void populate_compose_output_groups(const workspace_projection_context_t& projection,
                                    wormedit::chart_id_t chart) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    const auto& groups = projection.document.output_groups;
    const bool project = projection.source.active_tab == wormedit::workspace_tab_t::setup;
    const auto selected_count = std::ranges::count_if(groups, [&](const auto& group) {
        return project || std::ranges::find(group.charts, chart) != group.charts.end();
    });
    const auto count = std::min(static_cast<std::size_t>(selected_count),
                                std::size_t{WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY});
    auto* arena = result.arena();
    result.compose.output_group.view = arena
        ? arena_array<WormUiComposeOutputGroupView>(*arena, count)
        : frame_span<WormUiComposeOutputGroupView>{};
    view.output_groups = result.compose.output_group.view.data();
    view.output_group_count = 0;
    view.output_groups_truncated = static_cast<std::size_t>(selected_count) > count;
    if (result.compose.output_group.view.capacity() != count) {
        view.output_groups_truncated = true;
        return;
    }
    if (!arena) return;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (!project && std::ranges::find(groups[index].charts, chart) == groups[index].charts.end()) continue;
        if (view.output_group_count == count) break;
        if (!append_compose_output_group(result, *arena, groups[index], index)) {
            view.output_groups_truncated = true;
            break;
        }
    }
}

auto append_compose_resource(flattened_workspace_t& result, frame_arena_t& arena,
                             const compose_resource_entry_t& resource) -> bool {
    const auto* target = arena_string(arena, resource.target_index);
    const auto* name = arena_string(arena, resource.name);
    const auto* source = arena_string(arena, resource.source_name);
    const auto* path = arena_string(arena, resource.source_path);
    if (!target || !name || !source || !path) return false;
    std::uint8_t roles{};
    roles |= resource.sample ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE : 0;
    roles |= resource.bgm ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : 0;
    roles |= resource.bga ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA : 0;
    return result.compose.resource.view.push_back({
        .resource_id = resource.resource.value, .asset_id = resource.asset.value,
        .roles = roles, .audio_asset = resource.audio_asset,
        .selected_audio_asset = resource.selected_audio_asset,
        .image_asset = resource.image_asset, .selected_visual_asset = resource.selected_visual_asset,
        .missing = resource.missing, .target_index = target, .name = name, .source_name = source,
        .source_path = path, .use_count = resource.use_count});
}
void populate_compose_resources(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    const auto catalog = compose_resource_catalog_t::from(projection.document,
        projection.source.transport.audio_asset, WORM_UI_COMPOSE_RESOURCE_CAPACITY,
        projection.source.selected_visual_asset);
    auto* arena = result.arena();
    result.compose.resource.view = arena
        ? arena_array<WormUiComposeResourceView>(*arena, catalog.entries.size())
        : frame_span<WormUiComposeResourceView>{};
    view.resources = result.compose.resource.view.data();
    view.resource_count = 0;
    view.resource_total_count = catalog.total_count;
    view.resources_truncated = catalog.truncated;
    if (result.compose.resource.view.capacity() != catalog.entries.size()) {
        view.resources_truncated = true;
        return;
    }
    for (const auto& resource : catalog.entries) {
        if (!append_compose_resource(result, *arena, resource)) {
            view.resources_truncated = true;
            break;
        }
        view.resource_count = result.compose.resource.view.size();
    }
}

auto append_keysound_row(flattened_workspace_t& result, frame_arena_t& arena,
                         const wormedit::sys::keysound_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    const auto* name = arena_string(arena, row.name);
    const auto* path = arena_string(arena, row.source_path);
    if (!slot || !name || !path)
        return false;
    return result.compose.keysound.row.push_back(
        {.resource_id = row.resource.value,
         .slot = slot,
         .name = name,
         .missing = row.missing,
         .layer_mask = row.layer_mask,
         .note_layer_conflict_mask = row.note_layer_conflict_mask,
         .sound_group_id = row.sound_group_id,
         .use_count = row.use_count, .source_path = path});
}

auto append_keysound_selected_row(flattened_workspace_t& result, frame_arena_t& arena,
                                  const wormedit::sys::keysound_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    const auto* name = arena_string(arena, row.name);
    const auto* path = arena_string(arena, row.source_path);
    if (!slot || !name || !path) return false;
    return result.compose.keysound.selected_row.push_back({.resource_id = row.resource.value,
        .slot = slot, .name = name, .missing = row.missing, .layer_mask = row.layer_mask,
        .note_layer_conflict_mask = row.note_layer_conflict_mask,
        .sound_group_id = row.sound_group_id,
         .use_count = row.use_count, .source_path = path});
}

auto append_keysound_selection_row(flattened_workspace_t& result, frame_arena_t& arena,
                                   const wormedit::sys::keysound_selection_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    if (!slot) return false;
    return result.compose.keysound.selection_row.push_back({.resource_id = row.resource.value,
        .group_id = row.group_id, .layer_mask = row.layer_mask,
        .note_layer_conflict_mask = row.note_layer_conflict_mask,
        .slot = slot, .missing = row.missing});
}

auto append_keysound_display_row(flattened_workspace_t& result, frame_arena_t& arena,
                                 const wormedit::sys::keysound_list_t::display_row_t& row)
    -> bool {
    const auto* name = row.kind == wormedit::sys::keysound_list_t::display_row_t::kind_t::group_header
        ? arena_string(arena, row.group_name) : "";
    if (!name) return false;
    return result.compose.keysound.display_row.push_back({
        .kind = row.kind == wormedit::sys::keysound_list_t::display_row_t::kind_t::group_header
            ? WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER : WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
        .group_id = row.group_id, .group_name = name,
        .group_sound_count = row.group_sound_count, .expanded = row.expanded,
        .resource_id = row.resource.value});
}

auto bounded_text(const char* text, std::size_t size) -> std::string_view {
    if (!text) return {};
    const auto end = std::find(text, text + size, '\0');
    return {text, static_cast<std::size_t>(end - text)};
}

template<class T>
auto request_span(const T* data, std::size_t count) -> std::span<const T> {
    return data && count > 0 ? std::span<const T>{data, count} : std::span<const T>{};
}

auto build_compose_keysounds_view(const workspace_projection_context_t& projection,
                               const wormedit::chart_document_t& chart,
                               keysound_list_t& uncached)
    -> const keysound_list_t& {
    const auto& request = projection.keysound_request;
    if (request.slots_mode) {
        const auto sort = chart.source_family == wormedit::format_family_t::bms &&
                (request.sort_by_id || projection.keysound_sort == dock_sort_t::bms_id)
            ? dock_sort_t::bms_id : dock_sort_t::natural_name;
        const bool ids = chart.source_family == wormedit::format_family_t::bms;
        if (projection.keysound_cache)
            return build_keysound_list_cached_view(*projection.keysound_cache,
                projection.document, &chart, WORM_UI_COMPOSE_KEYSOUND_CAPACITY, sort, ids);
        uncached = build_keysound_list_view(projection.document, &chart,
            WORM_UI_COMPOSE_KEYSOUND_CAPACITY, sort, ids);
        return uncached;
    }
    auto layer_filter = wormedit::sys::keysound_layer_filter_kind_t::any;
    if (request.layer_filter_kind == 1)
        layer_filter = wormedit::sys::keysound_layer_filter_kind_t::no_layer;
    else if (request.layer_filter_kind == 2)
        layer_filter = wormedit::sys::keysound_layer_filter_kind_t::specific;
    const keysound_list_request_t projected_request{
            .search = bounded_text(request.automatic_search, sizeof(request.automatic_search)),
            .use_filter = static_cast<resource_use_filter_t>(request.use_filter),
            .selected_resource_ids = request_span(request.selected_resource_ids,
                                                   request.selected_resource_count),
            .include_selected_metadata = request.include_selected_metadata,
            .group_ids = request_span(request.group_filter_ids, request.group_filter_count),
            .group_filter_active = request.group_filter_active,
            .include_ungrouped = request.include_ungrouped,
            .layer_filter = layer_filter, .layer_id = request.layer_filter_id,
            .collapsed_group_ids = request_span(request.collapsed_group_ids,
                                                request.collapsed_group_count),
            .ungrouped_collapsed = request.ungrouped_collapsed,
            .first_row = request.automatic_first_row,
            .row_limit = request.automatic_row_limit == 0
                ? 64 : request.automatic_row_limit,
            .primary_resource_id = request.primary_resource_id,
            .filter_layer_first = request.filter_layer_first_row,
            .filter_layer_limit = request.filter_layer_row_limit == 0
                ? 64 : request.filter_layer_row_limit,
            .sort = request.sort_by_id || projection.keysound_sort == dock_sort_t::bms_id
                ? wormedit::sys::dock_sort_t::bms_id
                : wormedit::sys::dock_sort_t::natural_name,
            .ids_available = chart.source_family == wormedit::format_family_t::bms};
    if (projection.keysound_cache)
        return build_keysound_list_cached_view(*projection.keysound_cache,
            projection.document, &chart, projected_request);
    uncached = build_keysound_list_view(projection.document, &chart, projected_request);
    return uncached;
}

auto append_keysound_layer(flattened_workspace_t& result, frame_arena_t& arena,
                           const wormedit::sys::keysound_layer_t& layer) -> bool {
    const auto* name = arena_string(arena, layer.name);
    if (!name)
        return false;
    return result.compose.keysound.layer.push_back({.layer_id = layer.id.value,
                                                    .name = name,
                                                    .red = layer.color.red,
                                                    .green = layer.color.green,
                                                    .blue = layer.color.blue,
                                                    .alpha = layer.color.alpha,
                                                    .locked = layer.locked});
}

auto append_keysound_filter_layer(flattened_workspace_t& result, frame_arena_t& arena,
                                  const wormedit::sys::keysound_layer_t& layer) -> bool {
    const auto* name = arena_string(arena, layer.name);
    if (!name) return false;
    return result.compose.keysound.filter_layer.push_back({.layer_id = layer.id.value,
        .name = name, .red = layer.color.red, .green = layer.color.green,
        .blue = layer.color.blue, .alpha = layer.color.alpha, .locked = layer.locked});
}

void populate_compose_sound_groups(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    auto* arena = result.arena();
    if (!arena)
        return;
    auto& groups = result.compose.keysound.group;
    groups =
        arena_array<WormUiComposeSoundGroupView>(*arena, projection.document.sound_groups.size());
    for (const auto& group : projection.document.sound_groups) {
        const auto* name = arena_string(*arena, group.name);
        if (!name || !groups.push_back({.group_id = group.id.value, .name = name}))
            break;
    }
    view.compose_sound_groups = groups.data();
    view.compose_sound_group_count = groups.size();
}

void populate_compose_sound_slots(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    const auto& request = projection.keysound_request;
    view.sound_slots_mode = view.keysound_ids_available && request.slots_mode;
    if (!view.sound_slots_mode || !result.arena()) return;
    const auto list = wormedit::sys::build_sound_slots_view(projection.document,
        {.search = std::string{std::begin(request.search),
             std::find(std::begin(request.search), std::end(request.search), '\0')},
         .selected_slot = request.selected_slot,
         .first_row = request.first_row, .row_limit = request.row_limit,
         .use_filter = static_cast<resource_use_filter_t>(request.use_filter)});
    view.sound_slot_selected_number = request.selected_slot;
    view.sound_slot_selected_row = list.selected_row;
    view.sound_slot_first_row = list.first_row;
    view.sound_slot_total_matching = list.total_matching;
    auto& rows = result.compose.keysound.slot;
    auto& arena = *result.arena();
    rows = arena_array<WormUiComposeSoundSlotRowView>(arena, list.rows.size());
    for (const auto& row : list.rows) {
        const auto* slot = arena_string(arena, row.identifier);
        const auto* name = arena_string(arena, row.name);
        const auto* path = arena_string(arena, row.source_path);
        if (!slot || !name || !path || !rows.push_back({.number = row.number, .slot = slot,
            .resource_id = row.resource.value, .name = name, .use_count = row.use_count,
            .missing = row.missing, .placeable = row.placeable, .source_path = path})) break;
    }
    view.sound_slots = rows.data();
    view.sound_slot_count = rows.size();
}

auto reserve_compose_keysound_arrays(flattened_workspace_t& result, frame_arena_t& arena,
                                     const wormedit::sys::keysound_list_t& list)
    -> bool {
    auto& rows = result.compose.keysound;
    rows.row = arena_array<WormUiComposeKeysoundRowView>(arena, list.rows.size());
    rows.selected_row = arena_array<WormUiComposeKeysoundRowView>(arena, list.selected_rows.size());
    rows.selection_row = arena_array<WormUiComposeKeysoundSelectionView>(arena,
                                                                          list.selection_rows.size());
    rows.display_row = arena_array<WormUiComposeKeysoundDisplayRowView>(arena,
                                                                         list.display_rows.size());
    rows.advance_row = arena_array<WormUiComposeKeysoundAdvanceRowView>(arena,
                                                                        list.advance_rows.size());
    rows.listed_resource_id = arena_array<std::uint64_t>(arena, list.listed_resources.size());
    rows.all_resource_id = arena_array<std::uint64_t>(arena, list.all_resources.size());
    rows.layer = arena_array<WormUiComposeKeysoundLayerView>(arena, list.layers.size());
    rows.filter_layer = arena_array<WormUiComposeKeysoundLayerView>(arena,
                                                                     list.filter_layers.size());
    return rows.row.capacity() == list.rows.size() &&
        rows.selected_row.capacity() == list.selected_rows.size() &&
        rows.selection_row.capacity() == list.selection_rows.size() &&
        rows.display_row.capacity() == list.display_rows.size() &&
        rows.advance_row.capacity() == list.advance_rows.size() &&
        rows.listed_resource_id.capacity() == list.listed_resources.size() &&
        rows.all_resource_id.capacity() == list.all_resources.size() &&
        rows.layer.capacity() == list.layers.size() &&
        rows.filter_layer.capacity() == list.filter_layers.size();
}

void clear_compose_keysound_counts(WormUiComposeView& view) {
    view.keysound_count = 0;
    view.keysound_selected_row_count = 0;
    view.keysound_selection_row_count = 0;
    view.keysound_display_row_count = 0;
    view.keysound_advance_row_count = 0;
    view.keysound_listed_resource_count = 0;
    view.keysound_all_resource_count = 0;
    view.keysound_layer_count = 0;
    view.keysound_filter_layer_count = 0;
}

template<class KeysoundArrays>
void bind_compose_keysound_arrays(WormUiComposeView& view, KeysoundArrays& rows) {
    view.keysounds = rows.row.data();
    view.keysound_selected_rows = rows.selected_row.data();
    view.keysound_selection_rows = rows.selection_row.data();
    view.keysound_display_rows = rows.display_row.data();
    view.keysound_advance_rows = rows.advance_row.data();
    view.keysound_listed_resource_ids = rows.listed_resource_id.data();
    view.keysound_all_resource_ids = rows.all_resource_id.data();
    view.keysound_layers = rows.layer.data();
    view.keysound_filter_layers = rows.filter_layer.data();
}

void append_compose_keysound_payload(flattened_workspace_t& result, frame_arena_t& arena,
                                     const wormedit::sys::keysound_list_t& list,
                                     WormUiComposeView& view) {
    for (const auto& layer : list.layers)
        if (!append_keysound_layer(result, arena, layer)) break;
    view.keysound_layer_count = result.compose.keysound.layer.size();
    for (const auto& layer : list.filter_layers)
        if (!append_keysound_filter_layer(result, arena, layer)) break;
    view.keysound_filter_layer_count = result.compose.keysound.filter_layer.size();
    for (const auto& row : list.rows) {
        if (!append_keysound_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    }
    view.keysound_count = result.compose.keysound.row.size();
    for (const auto& row : list.selected_rows) {
        if (!append_keysound_selected_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    }
    view.keysound_selected_row_count = result.compose.keysound.selected_row.size();
    for (const auto& row : list.selection_rows)
        if (!append_keysound_selection_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    view.keysound_selection_row_count = result.compose.keysound.selection_row.size();
    for (const auto& row : list.display_rows) {
        if (!append_keysound_display_row(result, arena, row)) {
            view.keysound_display_rows_truncated = true;
            break;
        }
    }
    view.keysound_display_row_count = result.compose.keysound.display_row.size();
}

void append_compose_keysound_indices(flattened_workspace_t& result,
                                     const wormedit::sys::keysound_list_t& list,
                                     WormUiComposeView& view) {
    for (const auto& row : list.advance_rows)
        if (!result.compose.keysound.advance_row.push_back({.resource_id = row.resource.value,
                                                             .missing = row.missing})) {
            view.keysounds_truncated = true;
            break;
        }
    for (const auto id : list.listed_resources)
        if (!result.compose.keysound.listed_resource_id.push_back(id.value)) {
            view.keysounds_truncated = true;
            break;
        }
    for (const auto id : list.all_resources)
        if (!result.compose.keysound.all_resource_id.push_back(id.value)) {
            view.keysounds_truncated = true;
            break;
        }
    view.keysound_advance_row_count = result.compose.keysound.advance_row.size();
    view.keysound_listed_resource_count = result.compose.keysound.listed_resource_id.size();
    view.keysound_all_resource_count = result.compose.keysound.all_resource_id.size();
}

void populate_sound_picker(const workspace_projection_context_t& projection,
                           const wormedit::chart_document_t& chart) {
    auto& view = projection.result.view.compose.sound_picker;
    const auto& request = projection.keysound_request;
    if (!request.sound_picker_open) return;
    auto* arena = projection.result.arena();
    if (!arena) return;
    sound_picker_cache_t uncached;
    auto* cache = &uncached;
    if (projection.keysound_cache) {
        auto& stored = projection.keysound_cache->sound_picker;
        if (!stored) stored = std::make_shared<sound_picker_cache_t>();
        cache = stored.get();
    }
    const auto query = bounded_text(request.sound_picker_query, sizeof(request.sound_picker_query) - 1);
    const auto& result = build_sound_picker_view(*cache, projection.document, chart, query);
    std::copy(query.begin(), query.end(), view.query);
    view.total_count = result.total;
    for (const auto& row : result.rows) {
        const auto* label = arena_format(*arena, "%s%s%s%s  (#%llu)",
            row.slot.empty() ? "" : row.slot.c_str(), row.slot.empty() ? "" : " · ",
            row.name.empty() ? "(unnamed sound)" : row.name.c_str(),
            row.missing ? " · Missing" : "", static_cast<unsigned long long>(row.resource.value));
        if (!label) return;
        view.rows[view.row_count++] = {row.resource.value, label, row.missing};
    }
    view.ready = true;
}

void populate_compose_keysounds(const workspace_projection_context_t& projection,
                                wormedit::chart_id_t chart_id) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    auto* arena = result.arena();
    const auto chart = projection.document.find_chart(chart_id);
    view.keysounds_available = chart != nullptr;
    if (!arena || !view.keysounds_available) return;
    populate_sound_picker(projection, *chart);
    keysound_list_t uncached;
    const auto& list = build_compose_keysounds_view(projection, *chart, uncached);
    clear_compose_keysound_counts(view);
    view.keysound_section_projection_available = !projection.keysound_request.slots_mode;
    view.keysound_total_count = list.total;
    view.keysounds_truncated = list.truncated;
    view.keysound_display_total_count = list.display_total;
    view.keysound_display_first_row = list.display_first_row;
    view.keysound_display_rows_truncated = list.display_truncated;
    view.keysound_primary_display_index_valid = list.primary_display_index.has_value();
    view.keysound_primary_display_index = list.primary_display_index.value_or(0);
    const auto& request = projection.keysound_request;
    view.keysound_projected_primary_resource_id = request.primary_resource_id;
    view.keysound_projected_filters_unrestricted = !request.slots_mode &&
        !request.automatic_search[0] && !request.group_filter_active &&
        request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_ANY &&
        !request.collapsed_group_count && !request.ungrouped_collapsed && request.include_ungrouped;
    view.keysound_filter_layer_first = list.filter_layer_first;
    view.keysound_filter_layer_total = list.filter_layer_total;
    view.keysound_filter_selected_layer_valid = list.selected_filter_layer_valid;
    view.keysound_filter_selected_layer_name = arena_string(
        *arena, list.selected_filter_layer_name);
    view.keysound_ids_available = chart->source_family == wormedit::format_family_t::bms;
    view.keysound_ids_used = list.bms_capacity.used;
    view.keysound_ids_total = list.bms_capacity.total;
    view.keysound_largest_free_run = list.bms_capacity.largest_free_run;
    if (!reserve_compose_keysound_arrays(result, *arena, list)) {
        bind_compose_keysound_arrays(view, result.compose.keysound);
        view.keysounds_truncated = true;
        return;
    }
    bind_compose_keysound_arrays(view, result.compose.keysound);
    append_compose_keysound_payload(result, *arena, list, view);
    append_compose_keysound_indices(result, list, view);
    populate_compose_sound_groups(projection);
    populate_compose_sound_slots(projection);
}

void populate_compose_branch_rows(flattened_workspace_t& result,
                                  const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.rows.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY));
    view.branch_rows_truncated = outline.rows_truncated || count < outline.rows.size();
    if (count == 0) return;
    auto* arena = result.arena();
    result.compose.branch.row = arena ? arena_array<WormUiComposeBranchRowView>(*arena, count)
                                     : frame_span<WormUiComposeBranchRowView>{};
    view.branch_rows = result.compose.branch.row.data();
    if (result.compose.branch.row.capacity() != count) {
        view.branch_rows_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& row = outline.rows[index];
        const auto* text = arena_string(*arena, row.display_text);
        if (!text) {
            view.branch_rows_truncated = true;
            view.branch_display_text_truncated = true;
            break;
        }
        if (!result.compose.branch.row.push_back({.display_text = text, .depth = row.depth,
                .active = row.active, .selected = row.selected,
                .display_text_truncated = row.display_text_truncated,
                .has_source_order = row.source_order.has_value(),
                .source_order = row.source_order.value_or(0),
                .node_id = row.id.value,
                .raw_statement = row.kind == wormedit::bms_control_kind_t::raw_statement,
                .choice_block = row.kind == wormedit::bms_control_kind_t::random ||
                                row.kind == wormedit::bms_control_kind_t::switch_block})) {
            view.branch_rows_truncated = true;
            break;
        }
        view.branch_row_count = result.compose.branch.row.size();
    }
}
auto compose_branch_choice_view(const wormedit::workspace_bms_branch_choice_t& choice)
    -> WormUiComposeBranchChoiceView {
    return {.branch_id = choice.id.value,
        .kind = choice.kind == wormedit::bms_control_kind_t::switch_block
            ? WORM_UI_COMPOSE_BRANCH_SWITCH : WORM_UI_COMPOSE_BRANCH_RANDOM,
        .has_source_order = choice.source_order.has_value(),
        .source_order = choice.source_order.value_or(0), .active = choice.active,
        .editable = choice.editable, .has_selected_value = choice.selected_value.has_value(),
        .selected_value = choice.selected_value.value_or(0),
        .has_previous_value = choice.previous_value.has_value(),
        .previous_value = choice.previous_value.value_or(0),
        .has_next_value = choice.next_value.has_value(), .next_value = choice.next_value.value_or(0),
        .can_clear = choice.can_clear};
}
void populate_compose_branch_choices(flattened_workspace_t& result,
                                     const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.choices.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY));
    view.branch_choices_truncated = outline.choices_truncated || count < outline.choices.size();
    if (count == 0) return;
    auto* arena = result.arena();
    result.compose.branch.choice = arena ? arena_array<WormUiComposeBranchChoiceView>(*arena, count)
                                        : frame_span<WormUiComposeBranchChoiceView>{};
    view.branch_choices = result.compose.branch.choice.data();
    if (result.compose.branch.choice.capacity() != count) {
        view.branch_choices_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!result.compose.branch.choice.push_back(compose_branch_choice_view(outline.choices[index]))) {
            view.branch_choices_truncated = true;
            break;
        }
        view.branch_choice_count = result.compose.branch.choice.size();
    }
}
void populate_compose_branch_diagnostics(
    flattened_workspace_t& result, const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.diagnostics.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY));
    view.branch_diagnostics_truncated = outline.diagnostics_truncated || count < outline.diagnostics.size();
    if (count == 0) return;
    auto* arena = result.arena();
    for (std::size_t index = 0; index < count; ++index) {
        const auto* text = arena ? arena_string(*arena, outline.diagnostics[index].message) : nullptr;
        if (!text) {
            view.branch_diagnostics_truncated = true;
            break;
        }
        view.branch_diagnostics[view.branch_diagnostic_count++] = text;
    }
}
void populate_compose_branch_outline(flattened_workspace_t& result,
                                     const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    view.branch_outline_available = outline.available;
    view.branch_display_text_truncated = outline.display_text_truncated;
    populate_compose_branch_rows(result, outline);
    populate_compose_branch_choices(result, outline);
    populate_compose_branch_diagnostics(result, outline);
}

void populate_bookmark_chooser(const workspace_projection_context_t& projection, chart_id_t chart) {
    auto& result = projection.result;
    auto& view = result.view.compose.bookmark_chooser;
    view.project_id = projection.document.id.value;
    view.chart_id = chart.value;
    view.document_revision = projection.document.revision;
    const auto& request = projection.bookmark_chooser_request;
    if (!request.open) return;
    const auto page_outcome = bookmark_navigation_t::build_bookmark_chooser_view(projection.document, chart, request.first_row,
                                               std::clamp(request.row_limit, std::size_t{1}, std::size_t{64}), request.query);
    const auto& page = page_outcome ? *page_outcome : page_outcome.error();
    view.total_count = page.total_count;
    view.first_row = page.first_row;
    auto* arena = result.arena();
    if (!arena) { view.truncated = true; return; }
    view.query = arena_string(*arena, request.query);
    result.compose.bookmark_chooser_row = arena_array<WormUiBookmarkChooserRowView>(*arena, page.rows.size());
    for (const auto& row : page.rows) {
        WormUiBookmarkChooserRowView item{};
        item.bookmark_id = row.bookmark_id.value;
        item.anchor = static_cast<std::uint8_t>(row.anchor);
        item.position_label = arena_string(*arena, row.position_label);
        item.name = arena_string(*arena, row.name);
        item.display_label = arena_string(*arena, row.display_label);
        item.navigation_available = row.navigation_available;
        if (!item.position_label || !item.name || !item.display_label ||
            !result.compose.bookmark_chooser_row.push_back(item)) {
            view.truncated = true;
            break;
        }
    }
    view.rows = result.compose.bookmark_chooser_row.data();
    view.row_count = result.compose.bookmark_chooser_row.size();
}

void populate_compose_tab(const workspace_projection_context_t& projection, const wormedit::workspace_compose_view_t& tab) {
    auto& result = projection.result;
    const auto& source = projection.source;
                result.view.compose.visible = true;
                const auto& outline = tab.bms_branch_outline;
                auto& compose = result.view.compose;
                compose.selected_audio_asset_id =
                    source.transport.audio_asset.value_or(wormedit::asset_id_t{}).value;
                compose.selected_visual_asset_id =
                    source.selected_visual_asset.value_or(wormedit::asset_id_t{}).value;
                populate_compose_resources(projection);
                populate_compose_keysounds(projection, tab.chart);
                populate_bookmark_chooser(projection, tab.chart);
                populate_compose_branch_outline(result, outline);
                append_workspace_format(projection, "Measures: %zu", static_cast<std::size_t>(tab.measure_count));
                append_workspace_format(projection, "Notes: %zu", static_cast<std::size_t>(tab.note_count));
                append_workspace_format(projection, "Layout segments: %zu", static_cast<std::size_t>(tab.layout_segment_count));
                append_workspace_format(projection, "Visible lane definitions: %zu", static_cast<std::size_t>(tab.lane_count));
                append_workspace_format(projection, "Semantic layers: %zu", static_cast<std::size_t>(tab.layer_count));

}
void populate_storyboard_tab(const workspace_projection_context_t& projection, const wormedit::workspace_storyboard_view_t& tab) {
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));

}
void populate_sound_lane_modes(const workspace_projection_context_t& projection) {
    using namespace wormedit;
    const auto chart = projection.document.find_chart(chart_id_t{projection.result.view.selected_chart_id});
    if (chart == nullptr || chart->source_family != format_family_t::bmson) return;
    const auto bindings = bmson_lane_mode_resource_bindings(projection.document, chart->id);
    for (auto& row : projection.result.audio.sound_row) {
        const auto binding = std::ranges::find(bindings, resource_id_t{row.resource_id},
                                               &bmson_channel_resource_binding_t::resource);
        if (binding == bindings.end() || binding->ambiguous || !binding->channel) continue;
        row.bmson_channel_id = binding->channel->value;
        const auto channel = std::ranges::find(chart->sound_channels, *binding->channel, &sound_channel_t::id);
        if (channel != chart->sound_channels.end())
            row.bmson_channel_continues = std::ranges::any_of(channel->events,
                [](const auto& event) { return event.continuation; });
    }
}

void populate_sound_rows(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& audio = result.view.audio_slices;
    auto* arena = result.arena();
    wormedit::sys::sound_list_request_t request;
    request.row_limit = WORM_UI_SOUND_ROW_CAPACITY;
    const auto list = wormedit::sys::build_sound_list_view(projection.document, request);
    audio.sound_rows_truncated = list.total_matching > list.rows.size();
    result.audio.sound_row = arena ? arena_array<WormUiSoundRowView>(*arena, list.rows.size())
                                   : frame_span<WormUiSoundRowView>{};
    if (result.audio.sound_row.capacity() < list.rows.size()) {
        audio.sound_rows_truncated = true;
        return;
    }
    for (const auto& row : list.rows) {
        const auto resource = std::ranges::find(projection.document.resources, row.resource,
                                                &wormedit::resource_entry_t::id);
        const auto asset = resource == projection.document.resources.end()
                               ? wormedit::asset_id_t{}
                               : resource->asset;
        const auto properties =
            wormedit::sys::build_sound_properties_view(projection.document, row.resource);
        WormUiSoundRowView view{};
        view.resource_id = row.resource.value;
        view.asset_id = asset.value;
        view.name = arena_string(*arena, row.name);
        view.tags_text = arena_string(*arena, row.tags_text);
        view.source_path = arena_string(*arena, properties.source_path);
        view.kind = static_cast<int>(row.kind);
        view.use_count = row.use_count;
        view.duration_frames = row.duration_frames;
        view.sample_rate = row.sample_rate;
        view.missing = row.missing;
        (void)result.audio.sound_row.push_back(view);
    }
    populate_sound_lane_modes(projection);
    audio.sound_rows = result.audio.sound_row.data();
    audio.sound_row_count = result.audio.sound_row.size();
}
auto usage_position_label(const song_document_t& document, const sound_usage_row_t& row)
    -> std::string {
    if (!row.position)
        return "Source clip";
    const auto chart = document.find_chart(row.chart);
    if (chart == nullptr)
        return "Unavailable position";
    const auto measure =
        chart->find_measure(row.position->measure);
    if (measure == nullptr)
        return "Unavailable position";
    return "Measure " + std::to_string((measure - chart->measures.data()) + 1) +
           ", row " + std::to_string(row.position->row);
}

void populate_sound_usage(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.audio_slices;
    const auto& request = projection.sound_usage_request;
    view.usage_song_id = projection.document.id.value;
    view.usage_document_revision = projection.document.revision;
    if (request.selected_project_id != projection.document.id.value) return;
    view.selected_resource_id = request.selected_resource_id;
    view.selected_channel_id = request.selected_channel_id;
    view.selected_clip_id = request.selected_clip_id;
    if (!request.find_uses_open || !request.selected_resource_id)
        return;
    const auto usage =
        build_sound_usage_view(projection.document, resource_id_t{request.selected_resource_id});
    view.usage_song_id = usage.song.value;
    view.usage_document_revision = usage.revision;
    view.usage_resource_id = usage.resource.value;
    view.usage_asset_id = usage.asset.value;
    view.usage_valid = usage.valid;
    view.usage_total_count = usage.total_matching;
    view.usage_rows_truncated = usage.truncated;
    auto* arena = result.arena();
    if (!arena) {
        view.usage_rows_truncated = true;
        return;
    }
    result.audio.usage_row = arena_array<WormUiSoundUsageRowView>(*arena, usage.rows.size());
    for (const auto& row : usage.rows) {
        WormUiSoundUsageRowView item{};
        item.chart_id = row.chart.value;
        item.measure_id = row.position ? row.position->measure.value : 0;
        item.row = row.position ? row.position->row : 0;
        item.position_valid = row.position.has_value();
        item.object_id = row.object.value;
        item.channel_id = row.channel.value;
        item.clip_id = row.clip.value;
        item.referenced_resource_id = row.referenced_resource.value;
        item.referenced_asset_id = row.referenced_asset.value;
        item.kind = static_cast<int>(row.kind);
        item.scope = static_cast<int>(row.scope);
        item.chart_label = arena_string(*arena, row.chart_name);
        item.position_label = arena_string(*arena, usage_position_label(projection.document, row));
        if (!item.chart_label || !item.position_label || !result.audio.usage_row.push_back(item)) {
            view.usage_rows_truncated = true;
            break;
        }
    }
    view.usage_rows = result.audio.usage_row.data();
    view.usage_row_count = result.audio.usage_row.size();
}

void populate_audio_slices_tab(const workspace_projection_context_t& projection,
                               const wormedit::workspace_audio_slices_view_t& tab) {
    auto& result = projection.result;
    const auto append = [&](std::string line) {
        append_workspace_summary(projection, std::move(line));
    };
                auto& audio = result.view.audio_slices;
                audio.visible = true;
                populate_sound_rows(projection);
    populate_sound_usage(projection);
                audio.assets_truncated = tab.audio_assets.size() > WORM_UI_AUDIO_ASSET_CAPACITY;
                audio.asset_count =
                    std::min(tab.audio_assets.size(),
                             static_cast<std::size_t>(WORM_UI_AUDIO_ASSET_CAPACITY));
                auto* arena = result.arena();
                result.audio.asset = arena
                    ? arena_array<std::uint64_t>(*arena, audio.asset_count)
                    : frame_span<std::uint64_t>{};
                if (result.audio.asset.capacity() < audio.asset_count) {
                    audio.assets_truncated = true;
                    audio.asset_count = 0;
                }
                for (std::size_t index = 0; index < audio.asset_count; ++index)
                    (void)result.audio.asset.push_back(tab.audio_assets[index].value);
                audio.asset_ids = result.audio.asset.data();
                audio.has_selected_asset = tab.transport.audio_asset.has_value();
                audio.selected_asset_id =
                    tab.transport.audio_asset.value_or(wormedit::asset_id_t{}).value;
                append("Audio assets: " + std::to_string(tab.audio_assets.size()));
                append("Clips: " + std::to_string(tab.clip_count));
                append("Selected chart sound channels: " + std::to_string(tab.sound_channel_count));
                append(std::string("Transport: ") +
                       (tab.transport.playing ? "playing" : "stopped"));

}
void populate_compare_tab(const workspace_projection_context_t& projection, const wormedit::workspace_compare_validate_view_t& tab) {
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                append("Compared difficulties: " + std::to_string(tab.comparison_charts.size()));
                append("Validation errors: " + std::to_string(tab.error_count));
                append("Validation warnings: " + std::to_string(tab.warning_count));
                for (const auto& diagnostic : tab.diagnostics) {
                    append(diagnostic.message);
                }

}
struct workspace_tab_visitor {
    const workspace_projection_context_t& projection;
    void operator()(const wormedit::workspace_setup_view_t& tab) const {
        populate_setup_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_metadata_view_t& tab) const {
        populate_metadata_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_timing_view_t& tab) const {
        populate_timing_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_compose_view_t& tab) const {
        populate_compose_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_storyboard_view_t& tab) const {
        populate_storyboard_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_audio_slices_view_t& tab) const {
        populate_audio_slices_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_compare_validate_view_t& tab) const {
        populate_compare_tab(projection, tab);
    }
};

} // namespace

void flattened_workspace_t::rebind_view_pointers() {
    for (std::size_t index = 0; index < view.summary_line_count; ++index) {
        view.summary_lines[index] = summary.lines[index];
    }

    view.setup.stepmania_policy_status = setup.policy_status;

    auto& metadata_view = view.metadata;
    metadata_view.search_tags = metadata_view.search_tags_editable
                                   ? metadata.search_tag.data()
                                   : nullptr;
    metadata_view.search_tag_bytes = metadata_view.search_tags_editable
                                        ? metadata.search_tag_length.data()
                                        : nullptr;
    metadata_view.format_fields = metadata.format_field.data();
    metadata_view.format_targets = metadata.format_target.data();

    auto& compose_view = view.compose;
    compose_view.bookmark_chooser.rows = compose.bookmark_chooser_row.data();
    compose_view.resources = compose.resource.view.data();
    compose_view.keysounds = compose.keysound.row.data();
    compose_view.keysound_selected_rows = compose.keysound.selected_row.data();
    compose_view.keysound_selection_rows = compose.keysound.selection_row.data();
    compose_view.keysound_display_rows = compose.keysound.display_row.data();
    compose_view.keysound_advance_rows = compose.keysound.advance_row.data();
    compose_view.keysound_listed_resource_ids = compose.keysound.listed_resource_id.data();
    compose_view.keysound_all_resource_ids = compose.keysound.all_resource_id.data();
    compose_view.sound_slots = compose.keysound.slot.data();
    compose_view.keysound_layers = compose.keysound.layer.data();
    compose_view.keysound_filter_layers = compose.keysound.filter_layer.data();
    compose_view.output_groups = compose.output_group.view.data();
    compose_view.branch_rows = compose.branch.row.data();
    compose_view.branch_choices = compose.branch.choice.data();

    view.audio_slices.asset_ids = audio.asset.data();
    view.audio_slices.sound_rows = audio.sound_row.data();
    view.audio_slices.usage_rows = audio.usage_row.data();
}

auto build_notification_view(const notification_center_t& center, notification_time_t now)
    -> WormUiNotificationView {
    WormUiNotificationView result{};
    if (const auto* current = center.current_line()) {
        result.current_text = current->message.c_str();
        result.current_severity = static_cast<WormUiNotificationSeverity>(current->severity);
    }
    const auto unread = center.unread_counts();
    result.unread_warnings = unread.warnings;
    result.unread_errors = unread.errors;
    const auto entries = center.borrowed_snapshot(notification_capacity);
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if (entry->severity != notification_severity_t::warning &&
            entry->severity != notification_severity_t::error)
            continue;
        if (entry->severity == notification_severity_t::warning &&
            now - entry->timestamp >= std::chrono::seconds{8})
            continue;
        auto& toast = result.toasts[result.toast_count++];
        toast = {entry->id, static_cast<WormUiNotificationSeverity>(entry->severity),
                 entry->message.c_str(), entry->count};
        if (result.toast_count == WORM_UI_NOTIFICATION_TOAST_CAPACITY) break;
    }
    return result;
}

auto flatten_workspace(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured,
                       const wormedit::stepmania_export_policy_t& stepmania_policy,
                       frame_arena_t* arena, dock_sort_t keysound_sort,
                       WormUiKeysoundDockRequest keysound_request,
                       WormUiSoundUsageRequest sound_usage_request,
                       WormUiBookmarkChooserRequest bookmark_chooser_request,
                       keysound_list_cache_t* keysound_cache) -> flattened_workspace_t {
    flattened_workspace_t result(arena);
    result.view.project_title = document.metadata.title.c_str();
    result.view.artist = document.metadata.artist.c_str();
    result.view.project_id = document.id.value;
    result.view.document_revision = document.revision;
    result.view.selected_chart_id = source.selected_difficulty
        ? source.selected_difficulty->id.value : 0;
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
    populate_recovery_view(result, recovery, recovery_scan_configured);
    static_assert(workspace_tab_count == WORM_UI_WORKSPACE_TAB_COUNT);
    for (const auto& tab : workspace_tabs) {
        const auto index = static_cast<std::size_t>(tab.kind);
        result.view.tabs[index] = {
            .label = tab.kind == workspace_tab_t::timing ? nullptr : tab.label.data(),
            .enabled = is_workspace_tab_enabled(tab.kind),
            .id = static_cast<WormUiWorkspaceTab>(tab.kind),
        };
    }
    result.view.tab_heading = source.active_tab == workspace_tab_t::timing
        ? "Compose" : workspace_tabs[static_cast<std::size_t>(source.active_tab)].label.data();
    result.view.sound_kinds = sound_kind_view;
    result.view.new_project_layouts = new_project_layout_view;
    result.view.playback_rates = playback_rate_view;
    result.view.bms_encodings = bms_encoding_view;
    result.view.compose.timing_lanes = timing_lane_view;
    result.view.compose.timing_lane_captions = timing_caption_view;
    result.view.compose.note_palette = note_palette_view;
    result.view.setup.output_families = output_family_view;
    result.view.metadata.format_families = metadata_family_view;
    result.view.transport_rate_id = static_cast<std::uint32_t>(audio::playback_rate_id_t::normal);

    const workspace_projection_context_t projection{
        result,        source,           document,           stepmania_policy,
        keysound_sort, keysound_request, sound_usage_request, bookmark_chooser_request,
        keysound_cache};
    std::visit(workspace_tab_visitor{projection}, source.tab_view);
    // Export is project UI and must not depend on building a Compose scene.
    const auto* compose_tab = std::get_if<workspace_compose_view_t>(&source.tab_view);
    const auto selected = source.selected_difficulty ? source.selected_difficulty->id
        : compose_tab ? compose_tab->chart : chart_id_t{};
    result.view.compose.project_id = document.id.value;
    result.view.compose.chart_id = selected.value;
    result.view.compose.document_revision = document.revision;
    populate_compose_output_groups(projection, selected);
    result.view.audio_slices.sound_kinds = sound_kind_view;
    return result;
}

} // namespace wormedit::sys

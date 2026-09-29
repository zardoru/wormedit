#include "sys/workspace_projection.h"

#include "sys/compose_resource_presenter.h"
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
namespace {

constexpr std::array<const char*, WORM_UI_WORKSPACE_TAB_COUNT> tab_labels{
    "Setup", "Metadata", "Timing", "Compose", "Storyboard", "Audio / Slices", "Compare / Validate",
};

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

void populate_recovery_view(
    flattened_workspace_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery,
    bool recovery_scan_configured) {
    result.view.recovery_panel_visible = false;
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
        result.recovery.path[index] = row.path_label;
        auto& summary = result.recovery.summary[index];
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
        auto& disabled_reason = result.recovery.disabled_reason[index];
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
            .path_label = result.recovery.path[index].c_str(),
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
    result.view.recovery_panel_visible = recovery_row_count != 0 ||
                                         recovery_diagnostic_count != 0 ||
                                         result.view.recovery_rows_truncated;
    for (std::size_t index = 0; index < recovery_diagnostic_count; ++index) {
        auto& diagnostic = result.recovery.diagnostic[index];
        diagnostic = recovery.diagnostics[index].message;
        if (!recovery.diagnostics[index].path.empty()) {
            diagnostic += " (" + recovery.diagnostics[index].path.generic_string() + ")";
        }
        result.view.recovery_diagnostics[index] = diagnostic.c_str();
    }
}

struct workspace_projection_context_t {
    flattened_workspace_t& result;
    const wormedit::workspace_view_t& source;
    const wormedit::song_document_t& document;
    const wormedit::stepmania_export_policy_t& stepmania_policy;
};

void append_workspace_summary(const workspace_projection_context_t& projection,
                              std::string line) {
    auto& result = projection.result;
    if (result.view.summary_line_count >= result.summary.lines.size()) return;
    const auto index = result.view.summary_line_count++;
    result.summary.lines[index] = std::move(line);
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
                const auto resolved = wormedit::resolve_stepmania_export_policy(stepmania_policy);
                setup.stepmania_policy_valid = static_cast<bool>(resolved);
                if (resolved) {
                    result.setup.policy_status =
                        stepmania_policy.file_profile == wormedit::stepmania_profile_t::sm &&
                                stepmania_policy.timing_syntax ==
                                    wormedit::stepmania_timing_syntax_t::native_warps
                            ? "Valid syntax policy; SM charts containing warps require a legacy "
                              "encoding instead."
                            : "Compatible StepMania export policy.";
                } else {
                    result.setup.policy_status = resolved.message;
                }
                setup.stepmania_policy_status = result.setup.policy_status.c_str();

}

void append_metadata_format_targets(flattened_workspace_t& result, auto& metadata,
                                    const auto& groups, bool chart_scoped) {
    constexpr std::array format_families{
        wormedit::format_family_t::native_project, wormedit::format_family_t::bms,
        wormedit::format_family_t::stepmania,      wormedit::format_family_t::osu_mania,
        wormedit::format_family_t::bmson,          wormedit::format_family_t::midi,
        wormedit::format_family_t::quaver};
    std::array<const wormedit::format_metadata_t*, format_families.size()> first_groups{};
    std::array<std::size_t, format_families.size()> first_group_indices{};
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto family_index = static_cast<std::size_t>(groups[group_index].family);
        if (family_index < first_groups.size() && first_groups[family_index] == nullptr) {
            first_groups[family_index] = &groups[group_index];
            first_group_indices[family_index] = group_index;
        }
    }
    for (const auto family : format_families) {
        const auto family_index = static_cast<std::size_t>(family);
        const auto* found = first_groups[family_index];
        const auto index = metadata.format_target_count++;
        result.metadata.format_target[index] = {
            .chart_scoped = chart_scoped,
            .family = static_cast<std::uint8_t>(family),
            .group_exists = found != nullptr,
            .group_index = found == nullptr ? 0 : first_group_indices[family_index],
            .field_count = found == nullptr ? 0 : found->ordered_fields.size(),
        };
    }
}

void populate_metadata_format_targets(const workspace_projection_context_t& projection,
                                      const wormedit::workspace_metadata_view_t& tab,
                                      auto& metadata) {
    const auto& document = projection.document;
    auto& result = projection.result;
    append_metadata_format_targets(result, metadata, document.metadata.format_groups, false);
    if (!tab.selected_difficulty) return;
    const auto selected = std::ranges::find(document.charts, tab.selected_difficulty->id,
                                            &wormedit::chart_document_t::id);
    if (selected == document.charts.end()) return;
    metadata.selected_chart_source_family = static_cast<std::uint8_t>(selected->source_family);
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
                if (metadata.search_tags_editable) {
                    result.metadata.search_tag.reserve(tab.search_tags.size());
                    result.metadata.search_tag_length.reserve(tab.search_tags.size());
                    for (const auto& tag : tab.search_tags) {
                        result.metadata.search_tag.push_back(tag.c_str());
                        result.metadata.search_tag_length.push_back(tag.size());
                        metadata.search_tags_editable =
                            metadata.search_tags_editable &&
                            tag.size() <= WORM_UI_METADATA_SEARCH_TAG_BYTES;
                    }
                }
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
                metadata.format_fields_truncated =
                    tab.format_fields.size() > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY;
                metadata.format_field_total_count = tab.format_fields.size();
                metadata.format_field_count =
                    std::min(tab.format_fields.size(),
                             static_cast<std::size_t>(WORM_UI_METADATA_FORMAT_FIELD_CAPACITY));
                for (std::size_t index = 0; index < metadata.format_field_count; ++index) {
                    const auto& field = tab.format_fields[index];
                    result.metadata.format_field[index] = {
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
                metadata.format_fields = result.metadata.format_field.data();
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
                           std::string(format_family_name(field.family)) + " · " + field.name +
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
void populate_compose_tab(const workspace_projection_context_t& projection, const wormedit::workspace_compose_view_t& tab) {
    auto& result = projection.result;
    const auto& source = projection.source;
    const auto& document = projection.document;
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                result.view.compose.visible = true;
                const auto& outline = tab.bms_branch_outline;
                auto& compose = result.view.compose;
                compose.selected_audio_asset_id =
                    source.transport.audio_asset.value_or(wormedit::asset_id_t{}).value;
                compose.selected_visual_asset_id =
                    source.selected_visual_asset.value_or(wormedit::asset_id_t{}).value;
                const auto resource_catalog = wormedit::sys::make_compose_resource_catalog(
                    document, source.transport.audio_asset, WORM_UI_COMPOSE_RESOURCE_CAPACITY,
                    source.selected_visual_asset);
                compose.resources = result.compose.resource.view.data();
                compose.resource_count = resource_catalog.entries.size();
                compose.resource_total_count = resource_catalog.total_count;
                compose.resources_truncated = resource_catalog.truncated;
                compose.output_groups = result.compose.output_group.view.data();
                compose.output_group_count = 0;
                compose.output_groups_truncated = false;
                for (std::size_t group_index = 0; group_index < document.output_groups.size();
                     ++group_index) {
                    const auto& group = document.output_groups[group_index];
                    if (std::ranges::find(group.charts, tab.chart) == group.charts.end())
                        continue;
                    if (compose.output_group_count >= WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY) {
                        compose.output_groups_truncated = true;
                        continue;
                    }
                    const auto slot = compose.output_group_count++;
                    result.compose.output_group.name[slot] = group.name;
                    result.compose.output_group.path[slot] =
                        group.relative_output_path.generic_string();
                    const auto asset_directory =
                        wormedit::export_asset_directory(group.relative_output_path);
                    result.compose.output_group.asset_directory[slot] =
                        asset_directory ? asset_directory->generic_string() : std::string{};
                    result.compose.output_group.view[slot] = {
                        .document_index = group_index,
                        .name = result.compose.output_group.name[slot].c_str(),
                        .relative_output_path =
                            result.compose.output_group.path[slot].c_str(),
                        .relative_asset_directory =
                            result.compose.output_group.asset_directory[slot].c_str()};
                }
                for (std::size_t index = 0; index < resource_catalog.entries.size(); ++index) {
                    const auto& resource = resource_catalog.entries[index];
                    std::uint8_t roles{};
                    roles |= resource.sample ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE : 0;
                    roles |= resource.bgm ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : 0;
                    roles |= resource.bga ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA : 0;
                    auto& target_index = result.compose.resource.index[index];
                    target_index = resource.target_index;
                    auto& name = result.compose.resource.name[index];
                    name = resource.name;
                    auto& source_name = result.compose.resource.path[index];
                    source_name = resource.source_name;
                    result.compose.resource.view[index] = {
                        .resource_id = resource.resource.value,
                        .asset_id = resource.asset.value,
                        .roles = roles,
                        .audio_asset = resource.audio_asset,
                        .selected_audio_asset = resource.selected_audio_asset,
                        .image_asset = resource.image_asset,
                        .selected_visual_asset = resource.selected_visual_asset,
                        .missing = resource.missing,
                        .target_index = target_index.c_str(),
                        .name = name.c_str(),
                        .source_name = source_name.c_str(),
                    };
                }
                compose.branch_outline_available = outline.available;
                compose.branch_rows_truncated = outline.rows_truncated;
                compose.branch_choices_truncated = outline.choices_truncated;
                compose.branch_display_text_truncated = outline.display_text_truncated;
                compose.branch_diagnostics_truncated = outline.diagnostics_truncated;
                compose.branch_row_count =
                    std::min(outline.rows.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY));
                compose.branch_rows_truncated =
                    compose.branch_rows_truncated || compose.branch_row_count < outline.rows.size();
                for (std::size_t row_index = 0; row_index < compose.branch_row_count; ++row_index) {
                    const auto& row = outline.rows[row_index];
                    result.compose.branch.text[row_index] = row.display_text;
                    result.compose.branch.row[row_index] = {
                        .display_text = result.compose.branch.text[row_index].c_str(),
                        .depth = row.depth,
                        .active = row.active,
                        .selected = row.selected,
                        .display_text_truncated = row.display_text_truncated,
                        .has_source_order = row.source_order.has_value(),
                        .source_order = row.source_order.value_or(0),
                    };
                }
                compose.branch_rows = result.compose.branch.row.data();
                compose.branch_choice_count =
                    std::min(outline.choices.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY));
                compose.branch_choices_truncated =
                    compose.branch_choices_truncated ||
                    compose.branch_choice_count < outline.choices.size();
                for (std::size_t choice_index = 0; choice_index < compose.branch_choice_count;
                     ++choice_index) {
                    const auto& choice = outline.choices[choice_index];
                    result.compose.branch.choice[choice_index] = {
                        .branch_id = choice.id.value,
                        .kind = choice.kind == wormedit::bms_control_kind_t::switch_block
                                    ? WORM_UI_COMPOSE_BRANCH_SWITCH
                                    : WORM_UI_COMPOSE_BRANCH_RANDOM,
                        .has_source_order = choice.source_order.has_value(),
                        .source_order = choice.source_order.value_or(0),
                        .active = choice.active,
                        .editable = choice.editable,
                        .has_selected_value = choice.selected_value.has_value(),
                        .selected_value = choice.selected_value.value_or(0),
                        .has_previous_value = choice.previous_value.has_value(),
                        .previous_value = choice.previous_value.value_or(0),
                        .has_next_value = choice.next_value.has_value(),
                        .next_value = choice.next_value.value_or(0),
                        .can_clear = choice.can_clear,
                    };
                }
                compose.branch_choices = result.compose.branch.choice.data();
                compose.branch_diagnostic_count =
                    std::min(outline.diagnostics.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY));
                compose.branch_diagnostics_truncated =
                    compose.branch_diagnostics_truncated ||
                    compose.branch_diagnostic_count < outline.diagnostics.size();
                for (std::size_t diagnostic_index = 0;
                     diagnostic_index < compose.branch_diagnostic_count; ++diagnostic_index) {
                    auto& diagnostic = result.compose.branch.diagnostic[diagnostic_index];
                    diagnostic = outline.diagnostics[diagnostic_index].message;
                    compose.branch_diagnostics[diagnostic_index] = diagnostic.c_str();
                }
                append("Measures: " + std::to_string(tab.measure_count));
                append("Notes: " + std::to_string(tab.note_count));
                append("Layout segments: " + std::to_string(tab.layout_segment_count));
                append("Visible lane definitions: " + std::to_string(tab.lane_count));
                append("Semantic layers: " + std::to_string(tab.layer_count));

}
void populate_storyboard_tab(const workspace_projection_context_t& projection, const wormedit::workspace_storyboard_view_t& tab) {
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));

}
void populate_audio_slices_tab(const workspace_projection_context_t& projection, const wormedit::workspace_audio_slices_view_t& tab) {
    auto& result = projection.result;
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                auto& audio = result.view.audio_slices;
                audio.visible = true;
                audio.assets_truncated = tab.audio_assets.size() > WORM_UI_AUDIO_ASSET_CAPACITY;
                audio.asset_count =
                    std::min(tab.audio_assets.size(),
                             static_cast<std::size_t>(WORM_UI_AUDIO_ASSET_CAPACITY));
                result.audio.asset.reserve(audio.asset_count);
                for (std::size_t index = 0; index < audio.asset_count; ++index)
                    result.audio.asset.push_back(tab.audio_assets[index].value);
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
        view.summary_lines[index] = summary.lines[index].c_str();
    }

    for (std::size_t index = 0; index < view.recovery_candidate_count; ++index) {
        auto& candidate = view.recovery_candidates[index];
        candidate.path_label = recovery.path[index].c_str();
        candidate.summary = recovery.summary[index].c_str();
        candidate.disabled_reason = recovery.disabled_reason[index].empty()
                                        ? nullptr
                                        : recovery.disabled_reason[index].c_str();
    }
    for (std::size_t index = 0; index < view.recovery_diagnostic_count; ++index) {
        view.recovery_diagnostics[index] = recovery.diagnostic[index].c_str();
    }

    view.setup.stepmania_policy_status = setup.policy_status.c_str();

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
    compose_view.resources = compose.resource.view.data();
    for (std::size_t index = 0; index < compose_view.resource_count; ++index) {
        auto& resource = compose.resource.view[index];
        resource.target_index = compose.resource.index[index].c_str();
        resource.name = compose.resource.name[index].c_str();
        resource.source_name = compose.resource.path[index].c_str();
    }
    compose_view.output_groups = compose.output_group.view.data();
    for (std::size_t index = 0; index < compose_view.output_group_count; ++index) {
        auto& output_group = compose.output_group.view[index];
        output_group.name = compose.output_group.name[index].c_str();
        output_group.relative_output_path = compose.output_group.path[index].c_str();
        output_group.relative_asset_directory =
            compose.output_group.asset_directory[index].c_str();
    }
    compose_view.branch_rows = compose.branch.row.data();
    for (std::size_t index = 0; index < compose_view.branch_row_count; ++index) {
        compose.branch.row[index].display_text = compose.branch.text[index].c_str();
    }
    compose_view.branch_choices = compose.branch.choice.data();
    for (std::size_t index = 0; index < compose_view.branch_diagnostic_count; ++index) {
        compose_view.branch_diagnostics[index] = compose.branch.diagnostic[index].c_str();
    }

    view.audio_slices.asset_ids = audio.asset.data();
}

auto flatten_workspace(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured,
                       const wormedit::stepmania_export_policy_t& stepmania_policy)
    -> flattened_workspace_t {
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
    populate_recovery_view(result, recovery, recovery_scan_configured);
    for (std::size_t tab = 0; tab < tab_labels.size(); ++tab) {
        result.view.tabs[tab] = {
            .label = tab_labels[tab],
            .enabled = wormedit::workspace_tab_enabled(static_cast<wormedit::workspace_tab_t>(tab)),
        };
    }
    result.view.tab_heading = tab_labels[static_cast<std::size_t>(source.active_tab)];

    const workspace_projection_context_t projection{result, source, document, stepmania_policy};
    std::visit(workspace_tab_visitor{projection}, source.tab_view);
    return result;
}

} // namespace wormedit::sys

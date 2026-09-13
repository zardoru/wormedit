#include <wormedit/core/workspace.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto index_of(workspace_tab_t tab) noexcept -> std::optional<std::size_t> {
    const auto index = static_cast<std::size_t>(tab);
    if (index >= workspace_tab_count) {
        return std::nullopt;
    }
    return index;
}

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
    };
}

auto add_error(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message)
    -> void {
    diagnostics.push_back(error(code, std::move(message)));
}

auto has_errors(const diagnostic_list_t& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

template <typename id_t, typename projection_t>
auto validate_ids(const auto& values, projection_t projection, std::string_view label,
                  diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> seen;
    for (const auto& value : values) {
        const id_t id = projection(value);
        if (!id) {
            add_error(diagnostics, diagnostic_code_t::invalid_id,
                      std::string(label) + " has the reserved zero ID");
        } else if (!seen.insert(id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      std::string(label) + " ID is duplicated");
        }
    }
}

auto validate_binding_bounds(const song_document_t& document, const workspace_limits_t& limits)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (limits.maximum_comparison_charts < 2) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "workspace comparison chart cap must be at least two");
    }
    if (!document.id) {
        add_error(diagnostics, diagnostic_code_t::invalid_id,
                  "workspace cannot bind to a song with the reserved zero ID");
    }
    if (document.charts.size() > limits.maximum_charts) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace chart cap");
    }
    if (document.audio_assets.size() > limits.maximum_audio_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace audio asset cap");
    }
    if (document.visual_assets.size() > limits.maximum_visual_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace visual asset cap");
    }
    if (document.midi_assets.size() > limits.maximum_midi_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace MIDI asset cap");
    }
    if (document.resources.size() > limits.maximum_resources) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace resource cap");
    }
    if (document.storyboards.size() > limits.maximum_storyboards) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace storyboard cap");
    }
    validate_ids<chart_id_t>(
        document.charts, [](const auto& chart) { return chart.id; }, "chart", diagnostics);
    validate_ids<asset_id_t>(
        document.audio_assets, [](const auto& asset) { return asset.id; }, "audio asset",
        diagnostics);
    validate_ids<asset_id_t>(
        document.visual_assets, [](const auto& asset) { return asset.id; }, "visual asset",
        diagnostics);
    validate_ids<asset_id_t>(
        document.midi_assets, [](const auto& asset) { return asset.id; }, "MIDI asset",
        diagnostics);
    validate_ids<resource_id_t>(
        document.resources, [](const auto& resource) { return resource.id; }, "resource",
        diagnostics);
    std::unordered_set<std::uint64_t> asset_ids;
    for (const auto& asset : document.audio_assets) {
        if (asset.id) {
            asset_ids.insert(asset.id.value);
        }
    }
    for (const auto& asset : document.visual_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      "audio and visual assets share a duplicate ID");
        }
    }
    for (const auto& asset : document.midi_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      "audio, visual, and MIDI assets share a duplicate ID");
        }
    }
    validate_ids<storyboard_id_t>(
        document.storyboards, [](const auto& storyboard) { return storyboard.id; }, "storyboard",
        diagnostics);
    return diagnostics;
}

auto find_chart(const song_document_t& document, chart_id_t id) -> const chart_document_t* {
    const auto found =
        std::ranges::find_if(document.charts, [id](const auto& chart) { return chart.id == id; });
    return found == document.charts.end() ? nullptr : &*found;
}

auto find_audio_asset(const song_document_t& document, asset_id_t id) -> const audio_asset_t* {
    const auto found = std::ranges::find_if(document.audio_assets,
                                            [id](const auto& asset) { return asset.id == id; });
    return found == document.audio_assets.end() ? nullptr : &*found;
}

auto default_comparison_charts(const song_document_t& document, std::size_t maximum)
    -> std::vector<chart_id_t> {
    std::vector<chart_id_t> result;
    result.reserve(std::min({document.charts.size(), maximum, std::size_t{2}}));
    for (const auto& chart : document.charts) {
        if (result.size() == maximum || result.size() == 2) {
            break;
        }
        result.push_back(chart.id);
    }
    return result;
}

auto comparison_selection_valid(const song_document_t& document,
                                const std::vector<chart_id_t>& charts, std::size_t maximum)
    -> bool {
    if (charts.size() > maximum ||
        (document.charts.size() >= 2 ? charts.size() < 2
                                     : charts.size() > document.charts.size())) {
        return false;
    }
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(charts.size());
    return std::ranges::all_of(charts, [&](const auto chart) {
        return chart && seen.insert(chart.value).second && find_chart(document, chart) != nullptr;
    });
}

struct clip_location_t {
    const audio_clip_t* clip{};
    const sound_channel_t* channel{};
    bool duplicate{};
};

auto find_clip(const song_document_t& document, clip_id_t id) -> clip_location_t {
    clip_location_t result;
    for (const auto& chart : document.charts) {
        for (const auto& channel : chart.sound_channels) {
            if (channel.source_clip.id == id) {
                if (result.clip != nullptr) {
                    result.duplicate = true;
                } else {
                    result = {&channel.source_clip, &channel, false};
                }
            }
        }
    }
    return result;
}

auto contains_object_id(const song_document_t& document, object_id_t id) -> bool {
    if (!id) {
        return false;
    }
    for (const auto& chart : document.charts) {
        const auto in = [id](const auto& values) {
            return std::ranges::any_of(values, [id](const auto& value) { return value.id == id; });
        };
        if (in(chart.lane_layouts) || in(chart.bgm_source_lanes) || in(chart.bpms) ||
            in(chart.pauses) || in(chart.warps) || in(chart.scrolls) || in(chart.speeds) ||
            in(chart.kiai) || in(chart.timing_anchors) || in(chart.bookmarks) ||
            in(chart.sections) || in(chart.bga_events) || in(chart.sound_channels)) {
            return true;
        }
        for (const auto& measure : chart.measures) {
            if (in(measure.notes) || in(measure.bgm_events)) {
                return true;
            }
        }
        for (const auto& channel : chart.sound_channels) {
            if (in(channel.events)) {
                return true;
            }
        }
    }
    return false;
}

auto contains_focus_target(const song_document_t& document, const workspace_focus_target_t& target)
    -> bool {
    return std::visit(
        [&document](const auto& id) -> bool {
            using id_t = std::remove_cvref_t<decltype(id)>;
            if constexpr (std::is_same_v<id_t, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<id_t, chart_id_t>) {
                return static_cast<bool>(id) && find_chart(document, id) != nullptr;
            } else if constexpr (std::is_same_v<id_t, object_id_t>) {
                return contains_object_id(document, id);
            } else if constexpr (std::is_same_v<id_t, asset_id_t>) {
                return static_cast<bool>(id) &&
                       (find_audio_asset(document, id) != nullptr ||
                        std::ranges::any_of(document.visual_assets,
                                            [id](const auto& asset) { return asset.id == id; }) ||
                        std::ranges::any_of(document.midi_assets,
                                            [id](const auto& asset) { return asset.id == id; }));
            } else if constexpr (std::is_same_v<id_t, clip_id_t>) {
                const auto clip = find_clip(document, id);
                return static_cast<bool>(id) && clip.clip != nullptr && !clip.duplicate;
            } else if constexpr (std::is_same_v<id_t, resource_id_t>) {
                return static_cast<bool>(id) &&
                       std::ranges::any_of(document.resources, [id](const auto& resource) {
                           return resource.id == id;
                       });
            } else if constexpr (std::is_same_v<id_t, storyboard_id_t>) {
                return static_cast<bool>(id) &&
                       std::ranges::any_of(document.storyboards, [id](const auto& storyboard) {
                           return storyboard.id == id;
                       });
            }
            return false;
        },
        target);
}

auto make_chart_option(const chart_document_t& chart) -> workspace_chart_option_t {
    return {
        .id = chart.id,
        .difficulty_name = chart.metadata.name,
        .rating = chart.metadata.rating,
    };
}

auto checked_add(std::size_t& total, std::size_t value) -> bool {
    if (value > std::numeric_limits<std::size_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

auto enforce_summary_cap(std::size_t value, const workspace_controller_t& workspace,
                         std::string_view description, diagnostic_list_t& diagnostics) -> bool {
    if (value <= workspace.limits.maximum_summary_items) {
        return true;
    }
    add_error(diagnostics, diagnostic_code_t::invalid_resolution,
              std::string(description) + " exceeds the workspace summary cap");
    return false;
}

auto audio_clip_is_valid_for_asset(const audio_clip_t& clip, const audio_asset_t& asset) -> bool {
    if (clip.begin_frame >
        asset.stream.frame_count.value_or(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    if (!clip.end_frame.has_value()) {
        return true;
    }
    return *clip.end_frame >= clip.begin_frame &&
           *clip.end_frame <=
               asset.stream.frame_count.value_or(std::numeric_limits<std::uint64_t>::max());
}

} // namespace

auto workspace_tab_enabled(workspace_tab_t tab) noexcept -> bool {
    return index_of(tab).has_value() && tab != workspace_tab_t::storyboard;
}

auto make_workspace_controller(const song_document_t& document, workspace_limits_t limits)
    -> workspace_bind_result_t {
    workspace_bind_result_t result;
    result.diagnostics = validate_binding_bounds(document, limits);
    if (has_errors(result.diagnostics)) {
        return result;
    }

    workspace_controller_t workspace{
        .song = document.id,
        .document_revision = document.revision,
        .limits = limits,
    };
    if (!document.charts.empty()) {
        workspace.selected_chart = document.charts.front().id;
    }
    workspace.comparison_charts =
        default_comparison_charts(document, limits.maximum_comparison_charts);
    result.workspace = std::move(workspace);
    return result;
}

auto make_workspace_controller(const document_history_t& history, workspace_limits_t limits)
    -> workspace_bind_result_t {
    auto result = make_workspace_controller(history.document, limits);
    if (result.workspace.has_value()) {
        result.workspace->history = {.available = true,
                                     .undo_depth = history.undo.size(),
                                     .redo_depth = history.redo.size(),
                                     .dirty = is_document_dirty(history)};
    }
    return result;
}

auto rebind_workspace(workspace_controller_t& workspace, const song_document_t& document)
    -> workspace_action_result_t {
    workspace_action_result_t result;
    if (document.id != workspace.song) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace cannot be rebound to a different song identity");
        return result;
    }
    result.diagnostics = validate_binding_bounds(document, workspace.limits);
    if (has_errors(result.diagnostics)) {
        return result;
    }

    if (!index_of(workspace.active_tab).has_value() ||
        !workspace_tab_enabled(workspace.active_tab)) {
        workspace.active_tab = workspace_tab_t::setup;
    }
    if (!workspace.selected_chart.has_value() ||
        find_chart(document, *workspace.selected_chart) == nullptr) {
        workspace.selected_chart =
            document.charts.empty() ? std::nullopt : std::optional{document.charts.front().id};
    }
    if (!comparison_selection_valid(document, workspace.comparison_charts,
                                    workspace.limits.maximum_comparison_charts)) {
        workspace.comparison_charts =
            default_comparison_charts(document, workspace.limits.maximum_comparison_charts);
    }

    for (auto& focus : workspace.tab_focus) {
        if (focus.region.size() > workspace.limits.maximum_focus_region_bytes ||
            !contains_focus_target(document, focus.target)) {
            focus = {};
        }
    }

    if (workspace.transport.audio_asset.has_value() &&
        find_audio_asset(document, *workspace.transport.audio_asset) == nullptr) {
        workspace.transport = {};
    } else if (workspace.transport.audio_asset.has_value()) {
        const auto* asset = find_audio_asset(document, *workspace.transport.audio_asset);
        const auto frame_count = asset->stream.frame_count;
        if (frame_count.has_value()) {
            workspace.transport.position_frame =
                std::min(workspace.transport.position_frame, *frame_count);
        }
        if (workspace.transport.selected_clip.has_value()) {
            const auto clip = find_clip(document, *workspace.transport.selected_clip);
            if (clip.clip == nullptr || clip.duplicate || clip.clip->source != asset->id ||
                !audio_clip_is_valid_for_asset(*clip.clip, *asset)) {
                workspace.transport.selected_clip.reset();
            }
        }
        if (workspace.transport.loop_frames.has_value()) {
            const auto [begin, end] = *workspace.transport.loop_frames;
            if (begin >= end || (frame_count.has_value() && end > *frame_count)) {
                workspace.transport.loop_frames.reset();
            }
        }
    }

    workspace.document_revision = document.revision;
    result.applied = true;
    return result;
}

auto rebind_workspace(workspace_controller_t& workspace, const document_history_t& history)
    -> workspace_action_result_t {
    auto result = rebind_workspace(workspace, history.document);
    if (result) {
        workspace.history = {.available = true,
                             .undo_depth = history.undo.size(),
                             .redo_depth = history.redo.size(),
                             .dirty = is_document_dirty(history)};
    }
    return result;
}

auto apply_workspace_action(workspace_controller_t& workspace, const song_document_t& document,
                            std::uint64_t expected_revision, const workspace_action_t& action)
    -> workspace_action_result_t {
    workspace_action_result_t result;
    if (document.id != workspace.song || document.revision != workspace.document_revision ||
        expected_revision != workspace.document_revision) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace action refers to a stale or different document revision");
        return result;
    }

    auto proposal = workspace;
    const auto reject = [&result](diagnostic_code_t code, std::string message) {
        add_error(result.diagnostics, code, std::move(message));
    };
    std::visit(
        [&](const auto& typed_action) {
            using action_t = std::remove_cvref_t<decltype(typed_action)>;
            if constexpr (std::is_same_v<action_t, workspace_switch_tab_action_t>) {
                if (!index_of(typed_action.tab).has_value()) {
                    reject(diagnostic_code_t::invalid_id, "workspace tab value is invalid");
                } else if (!workspace_tab_enabled(typed_action.tab)) {
                    reject(diagnostic_code_t::unsupported_feature,
                           "storyboard editing is not available yet");
                } else {
                    proposal.active_tab = typed_action.tab;
                }
            } else if constexpr (std::is_same_v<action_t, workspace_select_chart_action_t>) {
                if (!typed_action.chart) {
                    reject(diagnostic_code_t::invalid_id,
                           "selected chart uses the reserved zero ID");
                } else if (find_chart(document, typed_action.chart) == nullptr) {
                    reject(diagnostic_code_t::invalid_id,
                           "selected chart does not exist in the bound document");
                } else {
                    proposal.selected_chart = typed_action.chart;
                }
            } else if constexpr (std::is_same_v<action_t,
                                                workspace_set_comparison_charts_action_t>) {
                if (typed_action.charts.size() < 2 ||
                    typed_action.charts.size() > workspace.limits.maximum_comparison_charts) {
                    reject(diagnostic_code_t::invalid_resolution,
                           "comparison requires two or more charts within its configured cap");
                } else {
                    std::unordered_set<std::uint64_t> charts;
                    for (const auto chart : typed_action.charts) {
                        if (!chart || find_chart(document, chart) == nullptr ||
                            !charts.insert(chart.value).second) {
                            reject(diagnostic_code_t::invalid_id,
                                   "comparison charts must be present and unique");
                            break;
                        }
                    }
                    if (!has_errors(result.diagnostics)) {
                        proposal.comparison_charts = typed_action.charts;
                    }
                }
            } else if constexpr (std::is_same_v<action_t, workspace_set_focus_action_t>) {
                const auto tab_index = index_of(typed_action.tab);
                if (!tab_index.has_value() || !workspace_tab_enabled(typed_action.tab)) {
                    reject(diagnostic_code_t::unsupported_feature,
                           "focus cannot be assigned to an invalid or disabled tab");
                } else if (typed_action.region.empty() ||
                           typed_action.region.size() >
                               workspace.limits.maximum_focus_region_bytes) {
                    reject(diagnostic_code_t::invalid_resolution,
                           "logical focus region is empty or exceeds its byte cap");
                } else if (!contains_focus_target(document, typed_action.target)) {
                    reject(diagnostic_code_t::invalid_id,
                           "logical focus target does not exist in the bound document");
                } else {
                    proposal.tab_focus[*tab_index] = {.region = typed_action.region,
                                                      .target = typed_action.target};
                }
            } else if constexpr (std::is_same_v<action_t, workspace_clear_focus_action_t>) {
                const auto tab_index = index_of(typed_action.tab);
                if (!tab_index.has_value()) {
                    reject(diagnostic_code_t::invalid_id, "workspace tab value is invalid");
                } else {
                    proposal.tab_focus[*tab_index] = {};
                }
            } else if constexpr (std::is_same_v<action_t, workspace_select_audio_asset_action_t>) {
                if (typed_action.asset.has_value() &&
                    (!*typed_action.asset ||
                     find_audio_asset(document, *typed_action.asset) == nullptr)) {
                    reject(diagnostic_code_t::invalid_id,
                           "selected audio asset does not exist in the bound document");
                } else if (proposal.transport.audio_asset != typed_action.asset) {
                    proposal.transport = {};
                    proposal.transport.audio_asset = typed_action.asset;
                }
            } else if constexpr (std::is_same_v<action_t, workspace_select_clip_action_t>) {
                if (!typed_action.clip.has_value()) {
                    proposal.transport.selected_clip.reset();
                } else {
                    const auto clip = find_clip(document, *typed_action.clip);
                    if (!*typed_action.clip || clip.clip == nullptr || clip.duplicate) {
                        reject(diagnostic_code_t::invalid_id,
                               "selected audio clip does not exist in the bound document");
                    } else {
                        const auto* asset = find_audio_asset(document, clip.clip->source);
                        if (asset == nullptr ||
                            !audio_clip_is_valid_for_asset(*clip.clip, *asset)) {
                            reject(diagnostic_code_t::invalid_id,
                                   "selected audio clip refers to an invalid source range");
                        } else {
                            proposal.transport.audio_asset = asset->id;
                            proposal.transport.selected_clip = typed_action.clip;
                            proposal.transport.position_frame = clip.clip->begin_frame;
                            proposal.transport.playing = false;
                            proposal.transport.loop_frames.reset();
                        }
                    }
                }
            } else if constexpr (std::is_same_v<action_t, workspace_seek_action_t>) {
                if (!proposal.transport.audio_asset.has_value()) {
                    reject(diagnostic_code_t::invalid_id,
                           "cannot seek without a selected audio asset");
                } else {
                    const auto* asset = find_audio_asset(document, *proposal.transport.audio_asset);
                    if (asset == nullptr || (asset->stream.frame_count.has_value() &&
                                             typed_action.frame > *asset->stream.frame_count)) {
                        reject(diagnostic_code_t::invalid_resolution,
                               "seek frame is outside the selected audio asset");
                    } else {
                        proposal.transport.position_frame = typed_action.frame;
                    }
                }
            } else if constexpr (std::is_same_v<action_t, workspace_set_playing_action_t>) {
                if (typed_action.playing && !proposal.transport.audio_asset.has_value()) {
                    reject(diagnostic_code_t::invalid_id,
                           "cannot start transport without a selected audio asset");
                } else {
                    proposal.transport.playing = typed_action.playing;
                }
            } else if constexpr (std::is_same_v<action_t, workspace_set_loop_action_t>) {
                if (!typed_action.frames.has_value()) {
                    proposal.transport.loop_frames.reset();
                } else if (!proposal.transport.audio_asset.has_value()) {
                    reject(diagnostic_code_t::invalid_id,
                           "cannot set a loop without a selected audio asset");
                } else {
                    const auto [begin, end] = *typed_action.frames;
                    const auto* asset = find_audio_asset(document, *proposal.transport.audio_asset);
                    if (begin >= end || asset == nullptr ||
                        (asset->stream.frame_count.has_value() &&
                         end > *asset->stream.frame_count)) {
                        reject(diagnostic_code_t::invalid_resolution,
                               "loop frame range is empty or outside the selected audio asset");
                    } else {
                        proposal.transport.loop_frames = typed_action.frames;
                    }
                }
            }
        },
        action);

    if (has_errors(result.diagnostics)) {
        return result;
    }
    workspace = std::move(proposal);
    result.applied = true;
    return result;
}

auto make_workspace_view(const workspace_controller_t& workspace, const song_document_t& document)
    -> workspace_view_result_t {
    return make_workspace_view(workspace, document, workspace.active_tab);
}

auto make_workspace_view(const workspace_controller_t& workspace, const song_document_t& document,
                         workspace_tab_t requested_tab) -> workspace_view_result_t {
    workspace_view_result_t result;
    if (document.id != workspace.song || document.revision != workspace.document_revision) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace view cannot be built from a stale or different document revision");
        return result;
    }
    result.diagnostics = validate_binding_bounds(document, workspace.limits);
    if (has_errors(result.diagnostics)) {
        return result;
    }
    const auto tab_index = index_of(requested_tab);
    if (!tab_index.has_value()) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace contains an invalid active tab value");
        return result;
    }
    if (workspace.selected_chart.has_value() &&
        find_chart(document, *workspace.selected_chart) == nullptr) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace selected chart no longer exists; rebind the workspace");
        return result;
    }
    if (!comparison_selection_valid(document, workspace.comparison_charts,
                                    workspace.limits.maximum_comparison_charts)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace comparison selection is invalid; rebind the workspace");
        return result;
    }
    const auto& focus = workspace.tab_focus[*tab_index];
    if ((!focus.region.empty() &&
         focus.region.size() > workspace.limits.maximum_focus_region_bytes) ||
        !contains_focus_target(document, focus.target)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace active-tab focus is invalid; rebind the workspace");
        return result;
    }

    std::optional<workspace_chart_option_t> selected_difficulty;
    const chart_document_t* chart = nullptr;
    if (workspace.selected_chart.has_value()) {
        chart = find_chart(document, *workspace.selected_chart);
        selected_difficulty = make_chart_option(*chart);
    }

    workspace_view_t view{
        .song = document.id,
        .document_revision = document.revision,
        .active_tab = requested_tab,
        .tab_enabled = workspace_tab_enabled(requested_tab),
        .selected_difficulty = selected_difficulty,
        .transport = workspace.transport,
        .history = workspace.history,
        .focus = focus,
    };
    switch (requested_tab) {
    case workspace_tab_t::setup: {
        if (!enforce_summary_cap(document.charts.size(), workspace, "chart list",
                                 result.diagnostics) ||
            !enforce_summary_cap(document.audio_assets.size(), workspace, "audio asset list",
                                 result.diagnostics) ||
            !enforce_summary_cap(document.visual_assets.size(), workspace, "visual asset list",
                                 result.diagnostics)) {
            return result;
        }
        workspace_setup_view_t content{
            .title = document.metadata.title,
            .artist = document.metadata.artist,
            .audio_asset_count = document.audio_assets.size(),
            .visual_asset_count = document.visual_assets.size(),
        };
        content.charts.reserve(document.charts.size());
        for (const auto& item : document.charts) {
            content.charts.push_back(make_chart_option(item));
        }
        view.tab_view = std::move(content);
        break;
    }
    case workspace_tab_t::metadata: {
        std::size_t item_count = document.metadata.ordered_search_tags.size();
        std::size_t text_bytes{};
        const auto account_text = [&](std::string_view text) {
            return checked_add(text_bytes, text.size()) &&
                   text_bytes <= workspace.limits.maximum_metadata_text_bytes;
        };
        const auto account_groups = [&](const auto& groups) {
            for (const auto& group : groups) {
                if (!checked_add(item_count, group.ordered_fields.size()))
                    return false;
                for (const auto& [name, value] : group.ordered_fields)
                    if (!account_text(name) || !account_text(value))
                        return false;
            }
            return true;
        };
        if (!account_text(document.metadata.title) || !account_text(document.metadata.artist)) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "metadata text exceeds the configured view byte cap");
            return result;
        }
        for (const auto& tag : document.metadata.ordered_search_tags)
            if (!account_text(tag)) {
                add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                          "metadata text exceeds the configured view byte cap");
                return result;
            }
        if ((chart != nullptr &&
             (!account_text(chart->metadata.name) || !account_text(chart->metadata.author))) ||
            !account_groups(document.metadata.format_groups) ||
            (chart != nullptr && !account_groups(chart->metadata.format_groups))) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "metadata field count or text bytes overflow the configured view bounds");
            return result;
        }
        if (!enforce_summary_cap(item_count, workspace, "metadata field list",
                                 result.diagnostics)) {
            return result;
        }
        workspace_metadata_view_t content{
            .title = document.metadata.title,
            .artist = document.metadata.artist,
            .search_tags = document.metadata.ordered_search_tags,
            .selected_difficulty = selected_difficulty,
            .chart_author = chart == nullptr ? std::string{} : chart->metadata.author,
        };
        content.format_fields.reserve(item_count - document.metadata.ordered_search_tags.size());
        const auto append_groups = [&](const auto& groups, bool chart_scoped) {
            constexpr auto format_family_count =
                static_cast<std::size_t>(format_family_t::quaver) + 1U;
            std::array<bool, format_family_count> seen_families{};
            for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                const auto& group = groups[group_index];
                const auto family_index = static_cast<std::size_t>(group.family);
                const auto first_group_for_family =
                    family_index < seen_families.size() && !seen_families[family_index];
                if (family_index < seen_families.size())
                    seen_families[family_index] = true;
                for (std::size_t field_index = 0; field_index < group.ordered_fields.size();
                     ++field_index) {
                    const auto& [name, value] = group.ordered_fields[field_index];
                    content.format_fields.push_back({
                        .chart_scoped = chart_scoped,
                        .family = group.family,
                        .first_group_for_family = first_group_for_family,
                        .group_index = group_index,
                        .field_index = field_index,
                        .group_field_count = group.ordered_fields.size(),
                        .name = name,
                        .value = value,
                        .source_order =
                            field_index < group.ordered_field_source_orders.size()
                                ? std::optional<
                                      std::size_t>{group.ordered_field_source_orders[field_index]}
                                : std::nullopt,
                    });
                }
            }
        };
        append_groups(document.metadata.format_groups, false);
        if (chart != nullptr)
            append_groups(chart->metadata.format_groups, true);
        view.tab_view = std::move(content);
        break;
    }
    case workspace_tab_t::timing: {
        if (chart == nullptr) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                      "timing tab requires a selected chart");
            return result;
        }
        std::size_t item_count = chart->measures.size();
        const auto counts_valid = checked_add(item_count, chart->bpms.size()) &&
                                  checked_add(item_count, chart->pauses.size()) &&
                                  checked_add(item_count, chart->warps.size()) &&
                                  checked_add(item_count, chart->bookmarks.size()) &&
                                  checked_add(item_count, chart->sections.size()) &&
                                  checked_add(item_count, chart->timing_anchors.size());
        if (!counts_valid) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "timing summary count overflows the supported range");
            return result;
        }
        if (!enforce_summary_cap(item_count, workspace, "timing summary", result.diagnostics)) {
            return result;
        }
        view.tab_view = workspace_timing_view_t{
            .chart = chart->id,
            .measure_count = chart->measures.size(),
            .bpm_count = chart->bpms.size(),
            .pause_count = chart->pauses.size(),
            .warp_count = chart->warps.size(),
            .bookmark_count = chart->bookmarks.size(),
            .section_count = chart->sections.size(),
            .anchor_count = chart->timing_anchors.size(),
        };
        break;
    }
    case workspace_tab_t::compose: {
        if (chart == nullptr) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                      "compose tab requires a selected chart");
            return result;
        }
        std::size_t note_count = 0;
        std::size_t lane_count = 0;
        bool counts_valid = true;
        for (const auto& measure : chart->measures) {
            counts_valid = counts_valid && checked_add(note_count, measure.notes.size());
        }
        for (const auto& layout : chart->lane_layouts) {
            counts_valid = counts_valid && checked_add(lane_count, layout.layout.lanes.size());
        }
        std::size_t item_count = chart->measures.size();
        counts_valid = counts_valid && checked_add(item_count, note_count) &&
                       checked_add(item_count, chart->lane_layouts.size()) &&
                       checked_add(item_count, lane_count) &&
                       checked_add(item_count, chart->semantic_layers.size());
        if (!counts_valid ||
            !enforce_summary_cap(item_count, workspace, "compose summary", result.diagnostics)) {
            if (!counts_valid) {
                add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                          "compose summary count overflows the supported range");
            }
            return result;
        }
        view.tab_view = workspace_compose_view_t{
            .chart = chart->id,
            .measure_count = chart->measures.size(),
            .note_count = note_count,
            .layout_segment_count = chart->lane_layouts.size(),
            .lane_count = lane_count,
            .layer_count = chart->semantic_layers.size(),
        };
        break;
    }
    case workspace_tab_t::storyboard:
        view.tab_view = workspace_storyboard_view_t{
            .enabled = false,
            .placeholder = "Storyboard editing is not available yet.",
            .storyboard_count = document.storyboards.size(),
        };
        break;
    case workspace_tab_t::audio_slices: {
        std::size_t clip_count = 0;
        for (const auto& item : document.charts) {
            if (!checked_add(clip_count, item.sound_channels.size())) {
                add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                          "audio clip count overflows the supported range");
                return result;
            }
        }
        if (!enforce_summary_cap(document.audio_assets.size(), workspace, "audio asset list",
                                 result.diagnostics) ||
            !enforce_summary_cap(clip_count, workspace, "audio clip list", result.diagnostics)) {
            return result;
        }
        workspace_audio_slices_view_t content{
            .clip_count = clip_count,
            .transport = workspace.transport,
        };
        content.audio_assets.reserve(document.audio_assets.size());
        for (const auto& asset : document.audio_assets) {
            content.audio_assets.push_back(asset.id);
        }
        if (chart != nullptr) {
            content.sound_channel_count = chart->sound_channels.size();
        }
        view.tab_view = std::move(content);
        break;
    }
    case workspace_tab_t::compare_validate: {
        auto diagnostics = validate_document(document);
        if (diagnostics.size() > workspace.limits.maximum_validation_diagnostics) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "document diagnostics exceed the workspace validation cap");
            return result;
        }
        workspace_compare_validate_view_t content{
            .comparison_charts = workspace.comparison_charts,
        };
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == diagnostic_severity_t::error) {
                ++content.error_count;
            } else if (diagnostic.severity == diagnostic_severity_t::warning) {
                ++content.warning_count;
            }
        }
        content.diagnostics = std::move(diagnostics);
        view.tab_view = std::move(content);
        break;
    }
    }
    result.view = std::move(view);
    return result;
}

} // namespace wormedit

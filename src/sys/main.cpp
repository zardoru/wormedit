#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "ui/frontend.h"
#include "wormedit/config.h"

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

namespace {

constexpr std::array<const char*, WORM_UI_WORKSPACE_TAB_COUNT> tab_labels{
    "Setup", "Metadata", "Timing", "Compose", "Storyboard", "Audio / Slices", "Compare / Validate",
};

auto initial_document() -> wormedit::song_document_t {
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
        .id = wormedit::song_id_t{1},
        .revision = 1,
        .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
        .charts = {std::move(chart)},
    };
}

struct flattened_workspace_t {
    WormUiWorkspaceView view{};
    std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> summary_storage;
};

auto flatten_workspace(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status)
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
                append("Song title: " + tab.title);
                append("Artist: " + tab.artist);
                append("Search tags: " + std::to_string(tab.search_tags.size()));
                append("Chart author: " +
                       (tab.chart_author.empty() ? std::string("(unset)") : tab.chart_author));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_timing_view_t>) {
                append("Measures: " + std::to_string(tab.measure_count));
                append("BPM events: " + std::to_string(tab.bpm_count));
                append("Stops / delays: " + std::to_string(tab.pause_count));
                append("Warps: " + std::to_string(tab.warp_count));
                append("Anchors: " + std::to_string(tab.anchor_count));
                append("Bookmarks / sections: " +
                       std::to_string(tab.bookmark_count + tab.section_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_compose_view_t>) {
                append("Measures: " + std::to_string(tab.measure_count));
                append("Notes: " + std::to_string(tab.note_count));
                append("Layout segments: " + std::to_string(tab.layout_segment_count));
                append("Visible lane definitions: " + std::to_string(tab.lane_count));
                append("Semantic layers: " + std::to_string(tab.layer_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_storyboard_view_t>) {
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_audio_slices_view_t>) {
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

} // namespace

int main() {
    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return 1;
    }

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

    auto history = wormedit::make_document_history(initial_document());
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
    std::string status = "Workspace ready";

    bool running = true;
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

        const auto model = wormedit::make_workspace_view(workspace, history.document);
        if (!model) {
            status = model.diagnostics.empty() ? "Workspace view unavailable"
                                               : model.diagnostics.front().message;
        } else {
            auto flattened = flatten_workspace(*model.view, history.document, status);
            for (std::size_t line = 0; line < flattened.view.summary_line_count; ++line) {
                flattened.view.summary_lines[line] = flattened.summary_storage[line].c_str();
            }
            const auto action = worm_ui_frontend_compose(&flattened.view);
            if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
                running = false;
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
                const auto applied = wormedit::apply_workspace_action(
                    workspace, history.document, history.document.revision,
                    wormedit::workspace_switch_tab_action_t{
                        static_cast<wormedit::workspace_tab_t>(action.tab)});
                status = applied
                             ? "Workspace tab changed"
                             : (applied.diagnostics.empty() ? "Workspace action rejected"
                                                            : applied.diagnostics.front().message);
            }
        }

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_ui_frontend_render(width, height);
        worm_window_swap(&window);
        SDL_Delay(1);
    }

    worm_ui_frontend_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}

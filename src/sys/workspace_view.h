#pragma once
#include "sys/compose_source_key.h"
#include <wormedit/core/semantic_layer_editor.h>
#include "sys/bookmark_chooser_view.h"
#include "sys/compose_dead_spans.h"
#include "sys/compose_sv_graph.h"
#include "sys/compose_recipe_areas.h"
#include "sys/metadata_projection_cache.h"
#include "sys/compose_resource_presenter.h"

#include "sys/sound_list_view.h"
#include "sys/sound_usage_view.h"
#include "sys/notifications/projection_notification_feedback.h"
#include "ui/frontend/workspace.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/waveform.h>
#endif
#include <wormedit/core/bmson_lane_mode.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/workspace.h>
#include <wormedit/formats/conversion.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit::sys {

struct application_frame_projection_context_t;

struct frontend_workspace_view_t {
    wormedit::ui::workspace_view_t view{};
    std::vector<projection_feedback_t> projection_feedback;

    std::size_t summary_size{};
    std::vector<std::pair<std::filesystem::path, std::string>> recovery_diagnostic_paths;
    void reset();

    struct {
        struct {
            std::vector<wormedit::exact_chart_row_t> row;
        } grid;
        std::vector<std::string> recipe_area_labels;
        std::vector<std::string> dead_span_labels;
        std::vector<std::pair<std::string, std::string>> timing_marker_labels;
        std::unordered_map<std::uint64_t, std::pair<std::string, std::string>> sound_labels;
        std::optional<std::pair<compose_scene_source_key_t, std::uint64_t>> sound_labels_source;
        std::vector<std::uint64_t> visible_sound_ids;
        std::unordered_map<std::uint64_t, std::string> stem_filenames;
        keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, std::uint64_t, std::string, std::uint16_t,
            std::size_t, std::size_t, resource_use_filter_t>, sound_slots_view_t> sound_slots_cache;
        std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
        struct {
            std::vector<dead_span_band_t> dead_bands;
            sv_graph_t sv_graph;
            sv_graph_t speed_graph;
            recipe_graph_t recipe_graph;
        } timing_geometry;
    } compose;
    keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, chart_id_t,
        std::uint64_t, std::size_t, std::size_t, std::array<char, 256>, bool>,
        bookmark_chooser_view_t> bookmark_chooser_cache;
    keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, chart_id_t,
        std::uint64_t, bool>, bool> output_group_binding;
    using semantic_window_cache_t = keyed_cache_t<std::tuple<const chart_document_t*, chart_id_t,
        std::uint64_t, std::uint64_t, std::size_t, std::size_t>, std::optional<semantic_layer_palette_window_t>>;
    semantic_window_cache_t semantic_window_cache, note_layer_window_cache;
    wormedit::ui::keysound_dock_request_t keysound_request;
    std::uint64_t query_session_generation{};
    keyed_cache_t<std::tuple<document_stamp_t, chart_id_t, std::uint64_t>,
        std::vector<bmson_channel_resource_binding_t>> sound_lane_modes_cache;
    sound_list_cache_t sound_list_cache;
    metadata_projection_cache_t metadata_projection_cache;
    sound_usage_cache_t sound_usage_cache;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::waveform_view_t audio_waveform_projection;
#endif

    [[nodiscard]] static auto from(const workspace_view_t& source,
                                   const song_document_t& document, const std::string& status,
                                   const native_project_recovery_controller_view_t& recovery,
                                   bool recovery_scan_configured,
                                   dock_sort_t keysound_sort = dock_sort_t::natural_name,
                                   const wormedit::ui::keysound_dock_request_t& keysound_request = {},
                                   wormedit::ui::sound_usage_request_t sound_usage_request = {},
                                   wormedit::ui::bookmark_chooser_request_t bookmark_chooser_request = {},
                                   keysound_list_cache_t* keysound_cache = nullptr,
                                   const bookmark_chooser_indices_t& bookmark_indices = {},
                                   compose_resource_catalog_cache_t* resource_catalog_cache = nullptr) -> frontend_workspace_view_t;

    void refill(const workspace_view_t& source,
                                   const song_document_t& document, const std::string& status,
                                   const native_project_recovery_controller_view_t& recovery,
                                   bool recovery_scan_configured,
                                   dock_sort_t keysound_sort = dock_sort_t::natural_name,
                                   const wormedit::ui::keysound_dock_request_t& keysound_request = {},
                                   wormedit::ui::sound_usage_request_t sound_usage_request = {},
                                   wormedit::ui::bookmark_chooser_request_t bookmark_chooser_request = {},
                                   keysound_list_cache_t* keysound_cache = nullptr,
                                   const bookmark_chooser_indices_t& bookmark_indices = {},
                                   compose_resource_catalog_cache_t* resource_catalog_cache = nullptr);

    void refill(const application_frame_projection_context_t& context);
};

} // namespace wormedit::sys

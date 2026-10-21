#pragma once
#include "sys/compose_source_key.h"
#include "sys/bookmark_chooser_view.h"
#include "sys/compose_dead_spans.h"
#include "sys/compose_sv_graph.h"
#include "sys/compose_recipe_areas.h"
#include "sys/metadata_projection_cache.h"
#include "sys/compose_resource_presenter.h"

#include "sys/sound_list_view.h"
#include "sys/sound_usage_view.h"
#include "sys/projection_notification_feedback.h"
#include "ui/frontend.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/waveform.h>
#endif
#include <wormedit/core/arena.h>
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
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit::sys {

// Per-unit storage is allocated once in fill order. Callers publish only size(),
// never capacity(), and set the existing unit truncation flag on failure.
template<class T>
class frame_span {
    static_assert(std::is_trivially_copyable_v<T>);
  public:
    frame_span() = default;
    explicit frame_span(std::span<T> storage) : storage_(storage) {}
    auto data() noexcept -> T* { return storage_.data(); }
    auto data() const noexcept -> const T* { return storage_.data(); }
    auto size() const noexcept -> std::size_t { return count_; }
    auto capacity() const noexcept -> std::size_t { return storage_.size(); }
    auto empty() const noexcept -> bool { return count_ == 0; }
    auto front() noexcept -> T& { return storage_.front(); }
    auto front() const noexcept -> const T& { return storage_.front(); }
    auto begin() noexcept -> T* { return data(); }
    auto end() noexcept -> T* { return count_ == 0 ? data() : data() + count_; }
    auto begin() const noexcept -> const T* { return data(); }
    auto end() const noexcept -> const T* { return count_ == 0 ? data() : data() + count_; }
    auto operator[](std::size_t index) noexcept -> T& { return storage_[index]; }
    auto operator[](std::size_t index) const noexcept -> const T& { return storage_[index]; }
    void clear() noexcept { count_ = 0; }
    [[nodiscard]] auto push_back(const T& value) noexcept -> bool {
        if (count_ == capacity()) return false;
        std::construct_at(data() + count_, value);
        ++count_;
        return true;
    }
  private:
    std::span<T> storage_;
    std::size_t count_{};
};

template<class T>
[[nodiscard]] auto arena_array(frame_arena_t& arena, std::size_t capacity) noexcept
    -> frame_span<T> {
    static_assert(std::is_trivially_copyable_v<T>);
    if (capacity == 0 || capacity > std::numeric_limits<std::size_t>::max() / sizeof(T))
        return {};
    const auto allocation = arena.allocate(capacity * sizeof(T), alignof(T));
    if (!allocation) return {};
    return frame_span<T>{std::span<T>{static_cast<T*>(*allocation), capacity}};
}
// Returns null on arena exhaustion; the publishing unit sets its existing
// truncation flag instead of exposing an unterminated or partial label.
[[nodiscard]] auto arena_string(frame_arena_t& arena, std::string_view value) noexcept
    -> const char*;
[[nodiscard]] auto arena_format(frame_arena_t& arena, const char* format, ...) noexcept
    -> const char*;

struct application_frame_projection_context_t;

struct frontend_workspace_view_t {
    frontend_workspace_view_t() noexcept = default;
    explicit frontend_workspace_view_t(frame_arena_t* arena) noexcept : arena_(arena) {}
    // Application frames borrow the resettable arena. Standalone/persistent
    // projections lazily own a separate arena whose lifetime follows copies.
    [[nodiscard]] auto arena() noexcept -> frame_arena_t*;
    [[nodiscard]] auto owned_arena_committed_bytes() const noexcept -> std::size_t {
        return owned_arena_ ? owned_arena_->committed_bytes() : 0;
    }
    wormedit::ui::workspace_view_t view{};
    std::vector<projection_feedback_t> projection_feedback;
    // Refresh pointers into inline storage after from returns or this object moves.
    void rebind_view_pointers();

    std::size_t summary_size{};
    std::vector<std::pair<std::filesystem::path, std::string>> recovery_diagnostic_paths;
    void reset();

    struct {
        std::string hover_tooltip;
        std::string selected_note_label;
        frame_span<WormUiBookmarkChooserRowView> bookmark_chooser_row;
        frame_span<WormUiComposeBookmarkView> bookmark;
        frame_span<WormUiComposeSectionView> section;
        struct {
            frame_span<WormUiComposeAudioBookmarkView> bookmark;
            frame_span<WormUiComposeAudioSectionView> section;
        } audio_marker;
        struct {
            std::vector<wormedit::exact_chart_row_t> row;
        } grid;
        std::vector<std::string> recipe_form_text;
        std::vector<std::string> recipe_area_labels;
        std::vector<std::string> dead_span_labels;
        std::vector<std::pair<std::string, std::string>> timing_marker_labels;
        std::unordered_map<std::uint64_t, std::pair<std::string, std::string>> sound_labels;
        std::optional<std::pair<compose_scene_source_key_t, std::uint64_t>> sound_labels_source;
        std::vector<std::uint64_t> visible_sound_ids;
        std::unordered_map<std::uint64_t, std::string> stem_filenames;
        std::vector<std::uint64_t> bmson_draft_playable_lane;
        std::vector<std::uint8_t> bmson_draft_restart;
        struct {
            frame_span<WormUiComposeSemanticLayerView> view;
            frame_span<WormUiComposeSemanticLayerView> note_menu;
        } semantic_layer;
        struct {
            frame_span<WormUiComposeBranchRowView> row;
            frame_span<WormUiComposeBranchChoiceView> choice;
        } branch;
        keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, std::uint64_t, std::string, std::uint16_t,
            std::size_t, std::size_t, resource_use_filter_t>, sound_slots_view_t> sound_slots_cache;
        struct {
            frame_span<WormUiComposeOutputGroupView> view;
        } output_group;
        std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
            lane_key{};
        std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
        struct {
            std::vector<dead_span_band_t> dead_bands;
            sv_graph_t sv_graph;
            sv_graph_t speed_graph;
            recipe_graph_t recipe_graph;
        } timing_geometry;
    } compose;
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
                                   frame_arena_t* arena = nullptr, dock_sort_t keysound_sort = dock_sort_t::natural_name,
                                   const wormedit::ui::keysound_dock_request_t& keysound_request = {},
                                   wormedit::ui::sound_usage_request_t sound_usage_request = {},
                                   WormUiBookmarkChooserRequest bookmark_chooser_request = {},
                                   keysound_list_cache_t* keysound_cache = nullptr,
                                   const bookmark_chooser_indices_t& bookmark_indices = {},
                                   compose_resource_catalog_cache_t* resource_catalog_cache = nullptr) -> frontend_workspace_view_t;

    void refill(const workspace_view_t& source,
                                   const song_document_t& document, const std::string& status,
                                   const native_project_recovery_controller_view_t& recovery,
                                   bool recovery_scan_configured,
                                   frame_arena_t* arena = nullptr, dock_sort_t keysound_sort = dock_sort_t::natural_name,
                                   const wormedit::ui::keysound_dock_request_t& keysound_request = {},
                                   wormedit::ui::sound_usage_request_t sound_usage_request = {},
                                   WormUiBookmarkChooserRequest bookmark_chooser_request = {},
                                   keysound_list_cache_t* keysound_cache = nullptr,
                                   const bookmark_chooser_indices_t& bookmark_indices = {},
                                   compose_resource_catalog_cache_t* resource_catalog_cache = nullptr);

    void refill(const application_frame_projection_context_t& context);
  private:
    frame_arena_t* arena_{};
    std::shared_ptr<frame_arena_t> owned_arena_;
};

} // namespace wormedit::sys

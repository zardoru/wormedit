#pragma once

#include "ui/frontend.h"

#include <wormedit/core/interaction.h>
#include <wormedit/core/arena.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/core/workspace.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <string>
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
    const auto allocation = arena.allocate_bytes(capacity * sizeof(T), alignof(T));
    if (!allocation) return {};
    return frame_span<T>{std::span<T>{static_cast<T*>(allocation.data), capacity}};
}
// Returns null on arena exhaustion; the publishing unit sets its existing
// truncation flag instead of exposing an unterminated or partial label.
[[nodiscard]] auto arena_string(frame_arena_t& arena, std::string_view value) noexcept
    -> const char*;
[[nodiscard]] auto arena_format(frame_arena_t& arena, const char* format, ...) noexcept
    -> const char*;

struct flattened_workspace_t {
    flattened_workspace_t() noexcept = default;
    explicit flattened_workspace_t(frame_arena_t* arena) noexcept : arena_(arena) {}
    // Application frames borrow the resettable arena. Standalone/persistent
    // projections lazily own a separate arena whose lifetime follows copies.
    [[nodiscard]] auto arena() noexcept -> frame_arena_t*;
    [[nodiscard]] auto owned_arena_committed_bytes() const noexcept -> std::size_t {
        return owned_arena_ ? owned_arena_->committed_bytes() : 0;
    }
    WormUiWorkspaceView view{};
    // Refresh pointers into inline storage after flatten_workspace returns or this object moves.
    void rebind_view_pointers();

    struct {
        std::array<const char*, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> lines{};
        bool truncated{};
    } summary;
    struct {
        const char* policy_status{""};
        bool text_truncated{};
    } setup;
    struct {
        frame_span<const char*> search_tag;
        frame_span<std::size_t> search_tag_length;
        frame_span<WormUiMetadataFormatFieldView> format_field;
        frame_span<WormUiMetadataFormatTargetView> format_target;
        // Arena-owned text is published directly; persistent sources retain ownership.
        bool text_truncated{};
        struct {
            frame_span<WormUiComposeOutputGroupView> view;
        } output_group;
    } metadata;
    struct {
        std::string status;
        std::string hover_tooltip;
        std::string selected_note_label;
        frame_span<WormUiComposeBookmarkView> bookmark;
        frame_span<WormUiComposeSectionView> section;
        struct {
            frame_span<WormUiComposeAudioBookmarkView> bookmark;
            frame_span<WormUiComposeAudioSectionView> section;
        } audio_marker;
        frame_span<WormUiComposeBandView> band;
        frame_span<WormUiComposeLaneView> lane;
        struct {
            frame_span<WormUiComposeGridLineView> line;
            frame_span<wormedit::exact_chart_row_t> row;
            std::vector<WormUiComposeGridLineView> proposed;
        } grid;
        std::vector<WormUiComposeTimingPreviewRectView> timing_preview_rect;
        std::vector<WormUiTimingMarkerView> timing_preview_marker;
        std::vector<WormUiTimingMarkerView> timing_marker;
        std::vector<WormUiComposeDeadSpanView> dead_span;
        std::vector<WormUiComposeKiaiSpanView> kiai_span;
        std::vector<WormUiComposeRecipeAreaView> recipe_area;
        std::vector<WormUiComposeSpeedSpanView> speed_span;
        std::vector<std::string> recipe_form_text;
        std::vector<WormUiComposeSvGraphPointView> recipe_graph_point;
        std::vector<std::string> recipe_area_labels;
        std::vector<WormUiComposeSvGraphPointView> sv_graph_point;
        std::vector<WormUiComposeSvGraphPointView> speed_graph_point;
        std::vector<std::string> dead_span_labels;
        std::vector<std::pair<std::string, std::string>> timing_marker_labels;
        frame_span<WormUiComposeDifferenceView> difference;
        frame_span<WormUiComposeHoldView> hold;
        frame_span<WormUiComposeNoteView> note;
        std::unordered_map<std::uint64_t, std::pair<std::string, std::string>> sound_labels;
        struct {
            frame_span<WormUiComposeBgmLaneView> lane;
            frame_span<WormUiComposeBgmEventView> event;
            std::vector<WormUiComposeBgmDraftView> draft;
            std::optional<WormUiComposeBgmMovePreviewView> move;
        } bgm;
        struct {
            frame_span<WormUiComposeWaveformRowView> row;
            frame_span<WormUiComposeWaveformOverviewRowView> overview_row;
            frame_span<WormUiComposeWaveformOverviewRowView> bar_row;
            frame_span<WormUiComposeBgmBarMarkView> bar_mark;
            frame_span<WormUiComposeBgmBarBookmarkView> bar_bookmark;
        } waveform;
        std::vector<std::uint64_t> bmson_draft_playable_lane;
        std::vector<std::uint8_t> bmson_draft_restart;
        std::optional<WormUiComposeCursorView> cursor;
        struct {
            frame_span<WormUiComposeSemanticLayerView> view;
            frame_span<WormUiComposeSemanticLayerView> note_menu;
        } semantic_layer;
        struct {
            frame_span<WormUiComposeBranchRowView> row;
            frame_span<WormUiComposeBranchChoiceView> choice;
        } branch;
        struct {
            frame_span<WormUiComposeResourceView> view;
        } resource;
        struct {
            frame_span<WormUiComposeKeysoundRowView> row;
            frame_span<WormUiComposeKeysoundLayerView> layer;
        } keysound;
        struct {
            frame_span<WormUiComposeOutputGroupView> view;
        } output_group;
        std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
            lane_key{};
        std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
    } compose;
    struct {
        frame_span<std::uint64_t> asset;
        frame_span<WormUiSoundRowView> sound_row;
        std::vector<WormUiAudioWaveformColumnView> waveform;
    } audio;
  private:
    frame_arena_t* arena_{};
    std::shared_ptr<frame_arena_t> owned_arena_;
};

[[nodiscard]] auto flatten_workspace(const workspace_view_t& source,
                                     const song_document_t& document, const std::string& status,
                                     const native_project_recovery_controller_view_t& recovery,
                                     bool recovery_scan_configured,
                                     const stepmania_export_policy_t& stepmania_policy,
                                     frame_arena_t* arena = nullptr)
    -> flattened_workspace_t;

// Borrowed notification strings remain valid until the center's next frame-
// thread mutation. Project after draining, then draw before dispatching actions.
[[nodiscard]] auto project_notification_view(const notification_center_t& center,
                                             notification_time_t now)
    -> WormUiNotificationView;

} // namespace wormedit::sys

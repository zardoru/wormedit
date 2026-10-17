#pragma once

#include "ui/frontend.h"

#include <cmath>
#include <cstdint>

enum class compose_drag_phase_t { cancel, update, click, commit };
enum class compose_drag_release_policy_t {
    latched_vertical,
    latched_planar,
    held_vertical,
    vertical_at_release,
    horizontal_at_release,
    clear_on_release,
    commit_on_release,
};

struct compose_drag_pointer_t {
    bool down{}, released{};
    double x{}, y{};
    auto operator==(const compose_drag_pointer_t&) const -> bool = default;
};

struct compose_drag_continuation_t {
    compose_drag_phase_t phase{compose_drag_phase_t::cancel};
    bool moved{};
    auto operator==(const compose_drag_continuation_t&) const -> bool = default;
};

// Screen pixels keep the drag threshold stable across viewport scaling and zoom.
// The RC1 registry owns cancellation; each emitter owns its verb-specific commit hook.
struct compose_drag_capture_t {
    std::uint64_t project{}, chart{}, revision{}, asset{};
    std::uint32_t sample_rate{};
    double anchor_x{}, anchor_y{};
    bool active{}, audio_source{};
    static constexpr double threshold_pixels = 4.0;
    static constexpr double edge_tolerance_pixels = 6.0;

    auto begin(const WormUiComposeView& view, double x, double y, bool locked,
               bool capture_audio = false) -> bool {
        cancel();
        if (!view.scene_available || view.input_occluded || locked) return false;
        project = view.project_id; chart = view.chart_id; revision = view.document_revision;
        asset = view.selected_audio_asset_id; sample_rate = view.waveform_overview_sample_rate;
        anchor_x = x; anchor_y = y; audio_source = capture_audio; active = true;
        return true;
    }
    auto matches(const WormUiComposeView& view, bool locked = false) const -> bool {
        return active && view.scene_available && !view.input_occluded && !locked &&
               project == view.project_id && chart == view.chart_id && revision == view.document_revision &&
               (!audio_source || (asset == view.selected_audio_asset_id &&
                                   sample_rate == view.waveform_overview_sample_rate));
    }
    auto moved_past_threshold(double x, double y, bool horizontal = false) const -> bool {
        return std::abs(y - anchor_y) >= threshold_pixels ||
               (horizontal && std::abs(x - anchor_x) >= threshold_pixels);
    }
    // A stack decision only: the caller supplies its source/runtime validity and
    // owns cancellation, payloads, click replay and terminal cleanup. Bookmark and
    // Audio Slices use their existing capture fields as a stack anchor snapshot.
    auto advance(compose_drag_pointer_t pointer, bool source_valid,
                 compose_drag_release_policy_t policy, bool was_moved = false) const
        -> compose_drag_continuation_t {
        if (!source_valid || (!pointer.down && !pointer.released)) return {};
        const auto moved = has_continuation_movement(pointer, policy, was_moved);
        if (!pointer.released) return {.phase = compose_drag_phase_t::update, .moved = moved};
        switch (policy) {
        case compose_drag_release_policy_t::clear_on_release:
            return {.phase = compose_drag_phase_t::click, .moved = moved};
        case compose_drag_release_policy_t::vertical_at_release:
        case compose_drag_release_policy_t::horizontal_at_release:
            return {.phase = moved ? compose_drag_phase_t::commit : compose_drag_phase_t::cancel,
                    .moved = moved};
        case compose_drag_release_policy_t::latched_vertical:
        case compose_drag_release_policy_t::latched_planar:
        case compose_drag_release_policy_t::held_vertical:
        case compose_drag_release_policy_t::commit_on_release:
            return {.phase = moved ? compose_drag_phase_t::commit : compose_drag_phase_t::click,
                    .moved = moved};
        }
        return {};
    }
    auto has_continuation_movement(compose_drag_pointer_t pointer,
                                   compose_drag_release_policy_t policy, bool was_moved) const -> bool {
        switch (policy) {
        case compose_drag_release_policy_t::latched_vertical:
            return was_moved || moved_past_threshold(pointer.x, pointer.y);
        case compose_drag_release_policy_t::latched_planar:
            return was_moved || moved_past_threshold(pointer.x, pointer.y, true);
        case compose_drag_release_policy_t::held_vertical:
            return was_moved || (!pointer.released && moved_past_threshold(pointer.x, pointer.y));
        case compose_drag_release_policy_t::vertical_at_release:
        case compose_drag_release_policy_t::clear_on_release:
            return moved_past_threshold(pointer.x, pointer.y);
        case compose_drag_release_policy_t::horizontal_at_release:
            return std::abs(pointer.x - anchor_x) >= threshold_pixels;
        case compose_drag_release_policy_t::commit_on_release:
            return true;
        }
        return false;
    }
    void cancel() { *this = {}; }
};

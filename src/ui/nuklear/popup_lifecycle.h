#pragma once

#include "wormedit/nuklear_config.h"
#include "ui/frontend.h"

#include <cstdint>
#include <initializer_list>

namespace wormedit::ui::chrome {

struct PopupContextStamp {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint64_t object_id{};

    static auto from(const WormUiComposeView& view, std::uint64_t object = 0) -> PopupContextStamp {
        return {view.project_id, view.chart_id, view.document_revision, object};
    }
    static auto from(const WormUiSetupView& view, std::uint64_t chart) -> PopupContextStamp {
        return {view.song_id, view.session_generation, view.document_revision, chart};
    }
};

struct retained_popup_t {
    bool open{};
    bool release_pending{};
    bool close_pending{};
    auto owns_input() const -> bool { return open || release_pending; }
    void cancel() {
        if (!open) return;
        open = false;
        release_pending = close_pending = true;
    }
};

// Window fills saved while a menu surface hides them (see chrome::menu_surface_begin).
struct MenuSurface {
    nk_color background{};
    nk_style_item fixed_background{};
};

inline auto popup_context_matches(PopupContextStamp expected,
                                  PopupContextStamp current) -> bool {
    return expected.project_id == current.project_id &&
           expected.chart_id == current.chart_id && expected.revision == current.revision &&
           expected.object_id == current.object_id;
}

inline auto popup_outside_press(const nk_context* context, struct nk_rect screen_bounds) -> bool {
    if (!context) return false;
    const auto& mouse = context->input.mouse;
    const auto pressed = nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
                         nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT);
    const auto coalesced_left = !mouse.buttons[NK_BUTTON_LEFT].down &&
                                mouse.buttons[NK_BUTTON_LEFT].clicked >= 2;
    const auto coalesced_right = !mouse.buttons[NK_BUTTON_RIGHT].down &&
                                 mouse.buttons[NK_BUTTON_RIGHT].clicked >= 2;
    return (pressed || coalesced_left || coalesced_right) &&
           !nk_input_is_mouse_hovering_rect(&context->input, screen_bounds);
}

inline auto popup_screen_bounds(nk_context* context, struct nk_rect local_bounds)
    -> struct nk_rect {
    if (!context) return local_bounds;
    const auto region = nk_window_get_content_region(context);
    return nk_rect(region.x + local_bounds.x, region.y + local_bounds.y,
                   local_bounds.w, local_bounds.h);
}

inline auto popup_dismiss_requested(const nk_context* context, struct nk_rect screen_bounds,
                                    bool was_open = true, bool include_escape = true) -> bool {
    if (!context) return false;
    const auto escape = include_escape &&
                        nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE);
    return escape || (was_open && popup_outside_press(context, screen_bounds));
}

inline void popup_arm_release(bool& release_pending) { release_pending = true; }

enum class PopupReleaseKeys { escape, text, all };

inline auto popup_key_has_input(const nk_input& input, nk_keys key) -> bool {
    return nk_input_is_key_down(&input, key) || nk_input_is_key_pressed(&input, key) ||
           nk_input_is_key_released(&input, key);
}

inline auto popup_input_is_quiet(const nk_context* context,
                                 PopupReleaseKeys keys = PopupReleaseKeys::escape) -> bool {
    if (!context) return false;
    const auto& input = context->input;
    for (const auto button : {NK_BUTTON_LEFT, NK_BUTTON_RIGHT}) {
        if (input.mouse.buttons[button].down || nk_input_is_mouse_pressed(&input, button) ||
            nk_input_is_mouse_released(&input, button)) return false;
    }
    if (keys == PopupReleaseKeys::all) {
        for (int key = 0; key < NK_KEY_MAX; ++key)
            if (popup_key_has_input(input, static_cast<nk_keys>(key))) return false;
        return true;
    }
    return !popup_key_has_input(input, NK_KEY_TEXT_RESET_MODE) &&
        (keys != PopupReleaseKeys::text || !popup_key_has_input(input, NK_KEY_ENTER));
}

inline auto popup_update_release_pending(const nk_context* context, bool& release_pending,
                                         PopupReleaseKeys keys = PopupReleaseKeys::escape) -> bool {
    if (release_pending && popup_input_is_quiet(context, keys)) release_pending = false;
    return release_pending;
}


// Return ownership for this frame before quiet input can retire the release guard.
inline auto release_retained_popup(const nk_context* context, retained_popup_t& state,
                                   PopupReleaseKeys keys) -> bool {
    const bool guarded = state.release_pending;
    (void)popup_update_release_pending(context, state.release_pending, keys);
    return guarded;
}

enum class retained_popup_begin_failure_t { cancel, retain };
enum class retained_popup_edit_focus_t { preserve, unfocus_on_close };
enum class retained_popup_close_trigger_t { closed, closed_or_pending };

struct retained_popup_frame_t {
    bool began{};
    bool guarded{};

    auto has_contents() const -> bool { return began; }
    auto owns_input() const -> bool { return began || guarded; }
    auto operator==(const retained_popup_frame_t&) const -> bool = default;
};

inline auto begin_retained_popup(nk_context* context, retained_popup_t& state,
                                const char* title, nk_flags flags, struct nk_rect bounds,
                                retained_popup_begin_failure_t failure)
    -> retained_popup_frame_t {
    if (nk_popup_begin(context, NK_POPUP_STATIC, title, flags, bounds))
        return {.began = true};
    if (failure == retained_popup_begin_failure_t::cancel) state.cancel();
    state.close_pending = false;
    const bool guarded = state.release_pending;
    if (failure == retained_popup_begin_failure_t::retain)
        (void)release_retained_popup(context, state, PopupReleaseKeys::all);
    return {.guarded = guarded};
}

inline void end_retained_popup(nk_context* context, retained_popup_t& state,
                               retained_popup_frame_t& popup,
                               retained_popup_edit_focus_t focus,
                               retained_popup_close_trigger_t trigger) {
    if (!popup.began) return;
    if (!state.open || (trigger == retained_popup_close_trigger_t::closed_or_pending &&
                        state.close_pending)) {
        if (focus == retained_popup_edit_focus_t::unfocus_on_close) nk_edit_unfocus(context);
        nk_popup_close(context);
        state.close_pending = false;
    }
    nk_popup_end(context);
    popup.began = false;
}


} // namespace wormedit::ui::chrome

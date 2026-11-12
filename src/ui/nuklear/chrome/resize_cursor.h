#pragma once

namespace wormedit::ui::nuklear {

// Pointer shape a widget asks for while the pointer is over, or dragging, a resize handle.
// The frontend clears the request when it begins reading input and applies it once per frame
// before rendering, so widgets only call request_resize_cursor() while they draw.
enum class resize_cursor_t { none, horizontal, vertical };

inline auto resize_cursor_request() noexcept -> resize_cursor_t& {
    static resize_cursor_t request{};
    return request;
}
// Horizontal wins over vertical when two handles ask in one frame.
inline void request_resize_cursor(resize_cursor_t cursor) noexcept {
    auto& request = resize_cursor_request();
    if (cursor == resize_cursor_t::horizontal || request == resize_cursor_t::none) request = cursor;
}

} // namespace wormedit::ui::nuklear

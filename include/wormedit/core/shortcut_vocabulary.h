#pragma once

#include <cstdint>

namespace wormedit {

// Chords use USB HID Keyboard/Keypad page usages, so a frontend can translate
// SDL, native, or other backend events without making the registry backend
// dependent. Modifier keys are represented separately from the key usage.
enum class shortcut_key_t : std::uint16_t {
    a = 0x04,
    b = 0x05,
    c = 0x06,
    d = 0x07,
    e = 0x08,
    f = 0x09,
    g = 0x0a,
    h = 0x0b,
    i = 0x0c,
    j = 0x0d,
    k = 0x0e,
    l = 0x0f,
    m = 0x10,
    n = 0x11,
    o = 0x12,
    p = 0x13,
    q = 0x14,
    r = 0x15,
    s = 0x16,
    t = 0x17,
    u = 0x18,
    v = 0x19,
    w = 0x1a,
    x = 0x1b,
    y = 0x1c,
    z = 0x1d,
    digit_1 = 0x1e,
    digit_2 = 0x1f,
    digit_3 = 0x20,
    digit_4 = 0x21,
    digit_5 = 0x22,
    digit_6 = 0x23,
    digit_7 = 0x24,
    digit_8 = 0x25,
    digit_9 = 0x26,
    digit_0 = 0x27,
    enter = 0x28,
    escape = 0x29,
    backspace = 0x2a,
    space = 0x2c,
    backquote = 0x35,
    f1 = 0x3a,
    f2 = 0x3b,
    f3 = 0x3c,
    f4 = 0x3d,
    f5 = 0x3e,
    f6 = 0x3f,
    f7 = 0x40,
    f8 = 0x41,
    f9 = 0x42,
    f10 = 0x43,
    f11 = 0x44,
    f12 = 0x45,
    delete_key = 0x4c,
    right = 0x4f,
    left = 0x50,
    down = 0x51,
    up = 0x52,
};

enum class shortcut_modifier_t : std::uint8_t {
    none = 0,
    shift = 1U << 0U,
    control = 1U << 1U,
    alt = 1U << 2U,
    meta = 1U << 3U,
    primary = 1U << 4U,
};

using shortcut_modifiers_t = std::uint8_t;

[[nodiscard]] constexpr auto operator|(shortcut_modifier_t left, shortcut_modifier_t right) noexcept
    -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(left) | static_cast<shortcut_modifiers_t>(right);
}

[[nodiscard]] constexpr auto operator|(shortcut_modifiers_t left,
                                       shortcut_modifier_t right) noexcept -> shortcut_modifiers_t {
    return left | static_cast<shortcut_modifiers_t>(right);
}

} // namespace wormedit

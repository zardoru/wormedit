#include "sys/shortcut_rows.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string>

using namespace wormedit;
using wormedit::sys::capture_shortcut_batch;
using wormedit::sys::build_shortcut_rows_view;
using wormedit::sys::shortcut_chord_text;
using wormedit::sys::shortcut_rows_projection_t;

namespace {

constexpr std::uint16_t letter_k_usage = 0x0e;
constexpr std::uint16_t letter_s_usage = 0x16;
constexpr std::uint16_t left_control_usage = 0xe0;

auto control_modifier() -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control);
}

auto shift_modifier() -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift);
}

auto command(std::string id, std::vector<shortcut_chord_t> shortcuts) -> command_registration_t {
    return {.definition = {.id = {id},
                           .display_name = id,
                           .localization_key = "command." + id,
                           .category = command_category_t::edit,
                           .focus_scope = command_focus_scope_t::global,
                           .default_shortcuts = std::move(shortcuts),
                           .enabled = true,
                           .invoke = [](const command_invocation_context_t&) {
                               return dispatch_outcome_t{.value = true};
                           }}};
}

auto event(std::uint16_t usage, shortcut_modifiers_t modifiers, bool repeat = false,
           bool tap_timing_space = false) -> WormUiShortcutEvent {
    return {.hid_usage = usage, .modifiers = modifiers, .repeat = repeat, .timestamp_ns = 0,
            .tap_timing_space = tap_timing_space};
}

auto row_conflicts(const shortcut_rows_projection_t& projection, std::size_t index) -> bool {
    return projection.rows[index].conflict;
}

} // namespace

TEST_CASE("chord text names modifiers in order and bound keys", "[sys][shortcut-rows]") {
    CHECK(shortcut_chord_text({.key = shortcut_key_t::z,
                               .modifiers = static_cast<shortcut_modifiers_t>(control_modifier() | shift_modifier())})
          == "Ctrl+Shift+Z");
    CHECK(shortcut_chord_text({.key = shortcut_key_t::f5, .modifiers = 0}) == "F5");
    CHECK(shortcut_chord_text({.key = shortcut_key_t::space, .modifiers = 0}) == "Space");
    CHECK(shortcut_chord_text({.key = shortcut_key_t::digit_0, .modifiers = 0}) == "0");
}

TEST_CASE("chord text is empty for a key the projection cannot name", "[sys][shortcut-rows]") {
    CHECK(shortcut_chord_text({.key = static_cast<shortcut_key_t>(0x01), .modifiers = 0}).empty());
}

auto register_unbound_and_bound(command_registry_t& registry) -> void {
    REQUIRE(register_command(registry, command("test.unbound", {})).value);
    REQUIRE(register_command(registry, command("test.bound",
        {shortcut_chord_t{.key = shortcut_key_t::s, .modifiers = control_modifier()}})).value);
}

void check_unbound_and_bound_rows(const shortcut_rows_projection_t& projection) {
    REQUIRE(projection.rows.size() == 2);
    CHECK(std::string(projection.rows[0].label) == "test.unbound");
    CHECK(std::string(projection.rows[0].chord) == "Unbound");
    CHECK(std::string(projection.rows[1].chord) == "Ctrl+S");
    CHECK_FALSE(row_conflicts(projection, 0));
    CHECK_FALSE(row_conflicts(projection, 1));
}

TEST_CASE("projection shows unbound commands and labels from ids", "[sys][shortcut-rows]") {
    command_registry_t registry;
    register_unbound_and_bound(registry);
    shortcut_rows_projection_t projection;
    build_shortcut_rows_view(registry, shortcut_primary_modifier_t::control, projection);
    check_unbound_and_bound_rows(projection);
}

auto register_conflicting_trio(command_registry_t& registry) -> void {
    const shortcut_chord_t shared{.key = shortcut_key_t::q, .modifiers = 0};
    REQUIRE(register_command(registry, command("test.one", {shared})).value);
    REQUIRE(register_command(registry, command("test.two", {shared})).value);
    REQUIRE(register_command(registry, command("test.three",
        {shortcut_chord_t{.key = shortcut_key_t::w, .modifiers = 0}})).value);
}

void check_conflicting_trio_rows(const shortcut_rows_projection_t& projection) {
    REQUIRE(projection.rows.size() == 3);
    CHECK(row_conflicts(projection, 0));
    CHECK(row_conflicts(projection, 1));
    CHECK_FALSE(row_conflicts(projection, 2));
}

TEST_CASE("projection flags every command in a conflicting pair", "[sys][shortcut-rows]") {
    command_registry_t registry;
    register_conflicting_trio(registry);
    shortcut_rows_projection_t projection;
    build_shortcut_rows_view(registry, shortcut_primary_modifier_t::control, projection);
    check_conflicting_trio_rows(projection);
}

TEST_CASE("capture takes the first usable press and ignores repeats", "[sys][shortcut-rows]") {
    const std::array<WormUiShortcutEvent, 2> batch{event(letter_k_usage, 0, true),
                                                   event(letter_s_usage, control_modifier())};
    const auto captured = capture_shortcut_batch(batch);
    REQUIRE(captured.chord.has_value());
    CHECK_FALSE(captured.cancel);
    CHECK(captured.chord->key == shortcut_key_t::s);
    CHECK(captured.chord->modifiers == control_modifier());
}

TEST_CASE("capture ignores modifier-only events, tap-timing presses and unknown usages", "[sys][shortcut-rows]") {
    const std::array<WormUiShortcutEvent, 3> batch{event(left_control_usage, control_modifier()),
                                                   event(letter_k_usage, 0, false, true),
                                                   event(0x01, 0)};
    const auto captured = capture_shortcut_batch(batch);
    CHECK_FALSE(captured.chord.has_value());
    CHECK_FALSE(captured.cancel);
}

TEST_CASE("capture of an empty batch captures nothing", "[sys][shortcut-rows]") {
    const auto captured = capture_shortcut_batch(std::span<const WormUiShortcutEvent>{});
    CHECK_FALSE(captured.chord.has_value());
    CHECK_FALSE(captured.cancel);
}

TEST_CASE("Escape without modifiers cancels a capture, and modified Escape is a chord", "[sys][shortcut-rows]") {
    const std::array<WormUiShortcutEvent, 1> plain{event(static_cast<std::uint16_t>(shortcut_key_t::escape), 0)};
    const auto canceled = capture_shortcut_batch(plain);
    CHECK(canceled.cancel);
    CHECK_FALSE(canceled.chord.has_value());

    const std::array<WormUiShortcutEvent, 1> modified{
        event(static_cast<std::uint16_t>(shortcut_key_t::escape), control_modifier())};
    const auto chord = capture_shortcut_batch(modified);
    CHECK_FALSE(chord.cancel);
    REQUIRE(chord.chord.has_value());
    CHECK(chord.chord->key == shortcut_key_t::escape);
}

namespace {

auto primary_modifier() -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary);
}

auto hid_chord(std::uint16_t usage, shortcut_modifiers_t modifiers) -> shortcut_chord_t {
    return {.key = static_cast<shortcut_key_t>(usage), .modifiers = modifiers};
}

} // namespace

TEST_CASE("punctuation keys have legends, so workspace.options reads Primary+,", "[sys][shortcut-rows]") {
    CHECK(shortcut_chord_text(hid_chord(0x36, primary_modifier())) == "Primary+,");
    CHECK(shortcut_chord_text(hid_chord(0x2d, 0)) == "-");
    CHECK(shortcut_chord_text(hid_chord(0x31, control_modifier())) == "Ctrl+\\");
}

TEST_CASE("keypad keys have legends", "[sys][shortcut-rows]") {
    CHECK(shortcut_chord_text(hid_chord(0x59, 0)) == "Keypad 1");
    CHECK(shortcut_chord_text(hid_chord(0x62, 0)) == "Keypad 0");
    CHECK(shortcut_chord_text(hid_chord(0x58, 0)) == "Keypad Enter");
}

TEST_CASE("navigation keys have legends", "[sys][shortcut-rows]") {
    CHECK(shortcut_chord_text(hid_chord(0x4f, 0)) == "Right");
    CHECK(shortcut_chord_text(hid_chord(0x4a, shift_modifier())) == "Shift+Home");
    CHECK(shortcut_chord_text(hid_chord(0x4e, 0)) == "PageDown");
}

TEST_CASE("capture accepts a punctuation key press", "[sys][shortcut-rows]") {
    const std::array<WormUiShortcutEvent, 1> batch{event(0x36, primary_modifier() & 0)};
    const auto captured = capture_shortcut_batch(batch);
    REQUIRE(captured.chord.has_value());
    CHECK(captured.chord->key == static_cast<shortcut_key_t>(0x36));
}

TEST_CASE("projection shows the primary modifier as the platform key", "[sys][shortcut-rows]") {
    command_registry_t registry;
    REQUIRE(register_command(registry, command("test.redo",
        {shortcut_chord_t{.key = shortcut_key_t::z,
                          .modifiers = static_cast<shortcut_modifiers_t>(primary_modifier() | shift_modifier())}})).value);
    shortcut_rows_projection_t projection;
    build_shortcut_rows_view(registry, shortcut_primary_modifier_t::control, projection);
    CHECK(std::string(projection.rows[0].chord) == "Ctrl+Shift+Z");
    build_shortcut_rows_view(registry, shortcut_primary_modifier_t::meta, projection);
    CHECK(std::string(projection.rows[0].chord) == "Shift+⌘+Z");
}

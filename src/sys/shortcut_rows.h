#pragma once

#include "ui/frontend.h"
#include <wormedit/core/command_registry.h>
#include <wormedit/core/keyed_cache.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {

struct shortcut_rows_lookup_t {
    const command_registry_t& registry;
    shortcut_primary_modifier_t primary;
};
struct shortcut_rows_key_t {
    struct entry_t {
        command_id_t id;
        std::string label;
        std::vector<shortcut_chord_t> bindings;
        command_focus_scope_t focus_scope{};
        std::optional<workspace_tab_t> tab;
        std::optional<lane_id_t> lane;
        std::string focus_context;
        auto operator==(const entry_t&) const -> bool = default;
    };
    std::vector<entry_t> entries;
    shortcut_primary_modifier_t primary{};
    auto operator==(const shortcut_rows_key_t&) const -> bool = default;
    [[nodiscard]] auto operator==(const shortcut_rows_lookup_t&) const -> bool;
    [[nodiscard]] static auto from(const shortcut_rows_lookup_t&) -> shortcut_rows_key_t;
    void assign(const shortcut_rows_lookup_t&);
};
struct shortcut_rows_data_t {
    std::vector<ui::shortcut_row_view_t> rows;
    std::vector<command_id_t> commands;
    std::vector<std::string> labels, chords;
};
class shortcut_rows_projection_t {
  public:
    void refill(const command_registry_t&, shortcut_primary_modifier_t);
    [[nodiscard]] auto rows() const noexcept -> const std::vector<ui::shortcut_row_view_t>& {
        return cache_.value().rows;
    }
    [[nodiscard]] auto commands() const noexcept -> const std::vector<command_id_t>& {
        return cache_.value().commands;
    }
  private:
    keyed_cache_t<shortcut_rows_key_t, shortcut_rows_data_t> cache_;
};

// Human-readable chord, for example "Ctrl+Shift+Z"; empty for an unbindable key.
[[nodiscard]] auto shortcut_chord_text(const shortcut_chord_t& chord) -> std::string;
// The same, with the primary modifier shown as the platform's key: "Ctrl+S", or "⌘+S" on macOS.
[[nodiscard]] auto shortcut_chord_text(shortcut_chord_t chord, shortcut_primary_modifier_t primary)
    -> std::string;

// Each registry command's chords as platform text ("Ctrl+Shift+Z", empty when unbound), for views
// rebuilt every frame such as the command palette. It reformats only when a binding or the primary
// modifier changes, so steady-state frames allocate nothing.
class command_shortcut_text_t {
  public:
    void refresh(const command_registry_t& registry, shortcut_primary_modifier_t primary);
    [[nodiscard]] auto text(std::size_t command_index) const noexcept -> const char*;
    [[nodiscard]] auto first_text(std::size_t command_index) const noexcept -> std::string_view;

  private:
    std::vector<std::vector<shortcut_chord_t>> bindings_;
    std::optional<shortcut_primary_modifier_t> primary_;
    std::vector<std::string> texts_;
    std::vector<std::string> first_texts_;
};

// Result of one frame's key batch while a capture is waiting. Escape without modifiers cancels.
struct shortcut_capture_t {
    bool cancel{};
    std::optional<shortcut_chord_t> chord;
};

// The first usable press in the batch: repeats, tap-timing presses, modifier-only events and
// unknown usages are skipped. An empty batch captures nothing.
[[nodiscard]] auto capture_shortcut_batch(std::span<const WormUiShortcutEvent> events)
    -> shortcut_capture_t;

} // namespace wormedit::sys

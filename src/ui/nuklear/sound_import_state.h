#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Pending "Import as…" list for the Sounds tab. Pure state: it never touches
// the filesystem, so the same rules serve drag-and-drop and the file picker.

inline constexpr std::size_t worm_ui_sound_import_capacity = 1'000;

// Values match wormedit::sound_kind_t (1 stem, 2 keysound, 3 sample).
enum worm_ui_sound_import_kind_t : std::uint8_t {
    WORM_UI_SOUND_IMPORT_STEM = 1,
    WORM_UI_SOUND_IMPORT_KEYSOUND = 2,
    WORM_UI_SOUND_IMPORT_SAMPLE = 3,
};

enum class worm_ui_sound_import_phase_t : std::uint8_t {
    closed,
    // One wav/ogg/flac file and no BGM yet: ask "stem or sound".
    choose_kind,
    // Several files: show the editable list and wait for Confirm.
    review_list,
    // One unambiguous file: import without a dialog.
    ready,
};

struct worm_ui_sound_import_item_t {
    std::string path;
    std::string name;
    std::uint8_t kind{WORM_UI_SOUND_IMPORT_KEYSOUND};
};

struct worm_ui_sound_import_state_t {
    worm_ui_sound_import_phase_t phase{worm_ui_sound_import_phase_t::closed};
    std::vector<worm_ui_sound_import_item_t> items;
    // Latest user-visible note, e.g. skipped files; empty when there is none.
    std::string message;
};

[[nodiscard]] auto worm_ui_nuklear_sound_import_is_audio_path(const std::string& path) -> bool;

// Adds dropped or picked paths to the pending list and chooses the phase.
// Non-audio files and duplicates are skipped with a message; more than the
// capacity are dropped with a message. Returns the number of files added.
auto worm_ui_nuklear_sound_import_add(worm_ui_sound_import_state_t& state,
                                      std::span<const std::string> paths, bool chart_has_bgm)
    -> std::size_t;

// Edits clamp to the list and ignore unknown kinds or indexes.
void worm_ui_nuklear_sound_import_set_kind(worm_ui_sound_import_state_t& state,
                                           std::size_t index, std::uint8_t kind);
void worm_ui_nuklear_sound_import_set_all_kinds(worm_ui_sound_import_state_t& state,
                                                std::uint8_t kind);
void worm_ui_nuklear_sound_import_rename(worm_ui_sound_import_state_t& state, std::size_t index,
                                         std::string name);
void worm_ui_nuklear_sound_import_remove(worm_ui_sound_import_state_t& state, std::size_t index);
// Moves the item by one place; delta is -1 (earlier) or +1 (later).
void worm_ui_nuklear_sound_import_move(worm_ui_sound_import_state_t& state, std::size_t index,
                                       int delta);

// Takes the items for import in list order and closes the dialog. Returns an
// empty list, leaving the state open with a message, when a name is empty.
[[nodiscard]] auto worm_ui_nuklear_sound_import_confirm(worm_ui_sound_import_state_t& state)
    -> std::vector<worm_ui_sound_import_item_t>;

// Discards the pending list and closes the dialog.
void worm_ui_nuklear_sound_import_cancel(worm_ui_sound_import_state_t& state);

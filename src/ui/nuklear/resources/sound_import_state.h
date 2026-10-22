#pragma once
#include "ui/frontend/sound_import.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <wormedit/core/vocabulary.h>
#include <wormedit/core/sound_group_proposal.h>
#include <wormedit/core/sound_import.h>

// Pending "Import as…" list for the Sounds tab. Pure state: it never touches
// the filesystem, so the same rules serve drag-and-drop and the file picker.

inline constexpr std::size_t worm_ui_sound_import_capacity = wormedit::maximum_bulk_sound_import;

enum class worm_ui_sound_import_phase_t : std::uint8_t {
    closed,
    // One wav/ogg/flac file and no BGM yet: ask "stem or sound".
    choose_kind,
    // Several files: show the editable list and wait for Confirm.
    review_list,
    // One or more filename groups can be accepted, edited or left ungrouped.
    review_groups,
    // One unambiguous file: import without a dialog.
    ready,
};

struct worm_ui_sound_import_item_t {
    std::string path;
    std::string name;
    wormedit::sound_kind_t kind{wormedit::sound_kind_t::keysound};
    std::optional<std::size_t> group_index;
};

struct worm_ui_sound_import_group_t {
    std::string name;
    std::optional<wormedit::sound_group_id_t> existing_group;
    bool accepted{true};
    bool expanded{};
};

struct worm_ui_sound_import_group_commit_t {
    std::string name;
    std::optional<wormedit::sound_group_id_t> existing_group;
    std::vector<std::size_t> member_indexes;
};

struct worm_ui_sound_import_commit_t {
    std::vector<worm_ui_sound_import_item_t> items;
    std::vector<worm_ui_sound_import_group_commit_t> groups;
};

struct worm_ui_sound_import_existing_group_t {
    wormedit::sound_group_id_t id;
    std::string name;
};

struct worm_ui_sound_import_state_t {
    worm_ui_sound_import_phase_t phase{worm_ui_sound_import_phase_t::closed};
    std::vector<worm_ui_sound_import_item_t> items;
    std::vector<worm_ui_sound_import_group_t> groups;
    // Latest user-visible note, e.g. skipped files; empty when there is none.
    std::string message;
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    wormedit::ui::sound_import_kind_t fixed_kind{};
    std::size_t member_first_row{};
    std::size_t member_row_limit{32};
    bool ungrouped_expanded{};
};

[[nodiscard]] auto worm_ui_nuklear_sound_import_is_audio_path(const std::string& path) -> bool;

// Adds dropped or picked paths to the pending list and chooses the phase.
// Non-audio files and duplicates are skipped with a message; more than the
// capacity are dropped with a message. Returns the number of files added.
auto worm_ui_nuklear_sound_import_add(worm_ui_sound_import_state_t& state,
                                      std::span<const std::string> paths, bool chart_has_bgm)
    -> std::size_t;

// Captures the target for stale-result checks and builds deterministic T-354
// proposals from the current staged paths and song groups.
void worm_ui_nuklear_sound_import_set_target(worm_ui_sound_import_state_t& state,
                                            std::uint64_t project, std::uint64_t chart,
                                            std::uint64_t revision);
void worm_ui_nuklear_sound_import_suggest_groups(
    worm_ui_sound_import_state_t& state,
    std::span<const worm_ui_sound_import_existing_group_t> existing_groups);
void worm_ui_nuklear_sound_import_set_group_accepted(worm_ui_sound_import_state_t& state,
                                                     std::size_t group, bool accepted);
void worm_ui_nuklear_sound_import_set_group_expanded(worm_ui_sound_import_state_t& state,
                                                    std::size_t group, bool expanded);
void worm_ui_nuklear_sound_import_set_ungrouped_expanded(
    worm_ui_sound_import_state_t& state, bool expanded);
void worm_ui_nuklear_sound_import_rename_group(worm_ui_sound_import_state_t& state,
                                              std::size_t group, std::string name);
// A null destination moves a member to Ungrouped. Destination indexes refer
// to the current proposal rows.
void worm_ui_nuklear_sound_import_move_member(worm_ui_sound_import_state_t& state,
                                             std::size_t member, std::optional<std::size_t> group);
void worm_ui_nuklear_sound_import_leave_all_ungrouped(worm_ui_sound_import_state_t& state);

// Edits clamp to the list and ignore unknown kinds or indexes.
void worm_ui_nuklear_sound_import_set_kind(worm_ui_sound_import_state_t& state,
                                           std::size_t index, wormedit::sound_kind_t kind);
void worm_ui_nuklear_sound_import_set_all_kinds(worm_ui_sound_import_state_t& state,
                                                wormedit::sound_kind_t kind);
void worm_ui_nuklear_sound_import_rename(worm_ui_sound_import_state_t& state, std::size_t index,
                                         std::string name);
void worm_ui_nuklear_sound_import_remove(worm_ui_sound_import_state_t& state, std::size_t index);
// Moves the item by one place; delta is -1 (earlier) or +1 (later).
void worm_ui_nuklear_sound_import_move(worm_ui_sound_import_state_t& state, std::size_t index,
                                       int delta);

// Takes the items for import in list order and closes the dialog. Returns an
// empty list, leaving the state open with a message, when a name is empty.
// Builds an immutable commit snapshot but retains all editable state until the
// application reports successful commit.
[[nodiscard]] auto worm_ui_nuklear_sound_import_confirm(worm_ui_sound_import_state_t& state)
    -> std::optional<worm_ui_sound_import_commit_t>;
void worm_ui_nuklear_sound_import_commit_failed(worm_ui_sound_import_state_t& state,
                                               std::string reason);
void worm_ui_nuklear_sound_import_commit_succeeded(worm_ui_sound_import_state_t& state);

// Discards the pending list and closes the dialog.
void worm_ui_nuklear_sound_import_cancel(worm_ui_sound_import_state_t& state);

// Locks section imports to their requested kind; zero retains generic import behavior.
void worm_ui_nuklear_sound_import_fix_kind(worm_ui_sound_import_state_t& state,
                                          wormedit::ui::sound_import_kind_t kind);

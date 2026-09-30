#include "sys/application_commands.h"

#include <charconv>
#include <string_view>
#include <utility>

namespace wormedit::sys {
namespace {

struct command_name_t {
    std::string_view id;
    application_command_kind_t kind;
};

constexpr command_name_t command_names[]{
    {"document.new_project", application_command_kind_t::document_new_project},
    {"document.new_chart", application_command_kind_t::document_new_chart},
    {"history.undo", application_command_kind_t::history_undo},
    {"history.redo", application_command_kind_t::history_redo},
    {"transport.play_pause", application_command_kind_t::transport_play_pause},
    {"transport.play_from_start", application_command_kind_t::transport_play_from_start},
    {"transport.stop", application_command_kind_t::transport_stop},
    {"transport.rate_slower", application_command_kind_t::transport_rate_slower},
    {"transport.rate_faster", application_command_kind_t::transport_rate_faster},
    {"edit.delete", application_command_kind_t::edit_delete},
    {"selection.clear", application_command_kind_t::selection_clear},
    {"selection.select_all", application_command_kind_t::selection_select_all},
    {"selection.invert", application_command_kind_t::selection_invert},
    {"navigation.previous_snap", application_command_kind_t::navigation_previous_snap},
    {"navigation.next_snap", application_command_kind_t::navigation_next_snap},
    {"navigation.previous_marker", application_command_kind_t::navigation_previous_marker},
    {"navigation.next_marker", application_command_kind_t::navigation_next_marker},
    {"workspace.open_setup", application_command_kind_t::workspace_open_setup},
    {"workspace.open_metadata", application_command_kind_t::workspace_open_metadata},
    {"workspace.open_timing", application_command_kind_t::workspace_open_timing},
    {"workspace.open_compose", application_command_kind_t::workspace_open_compose},
    {"workspace.open_storyboard", application_command_kind_t::workspace_open_storyboard},
    {"workspace.open_audio_slices", application_command_kind_t::workspace_open_audio_slices},
    {"workspace.open_compare_validate",
     application_command_kind_t::workspace_open_compare_validate},
    {"timing.toggle_lock", application_command_kind_t::timing_toggle_lock},
    {"timing.insert_bpm", application_command_kind_t::timing_insert_bpm},
    {"timing.insert_sv", application_command_kind_t::timing_insert_sv},
    {"timing.open_settings", application_command_kind_t::timing_open_settings},
    {"semantic_layer.clear_target", application_command_kind_t::semantic_layer_clear_target},
    {"semantic_layer.apply", application_command_kind_t::semantic_layer_apply},
    {"semantic_layer.remove", application_command_kind_t::semantic_layer_remove},
    {"semantic_layer.toggle", application_command_kind_t::semantic_layer_toggle},
    {"audio.paint_hitsound", application_command_kind_t::audio_paint_hitsound},
};

constexpr std::string_view semantic_layer_slot_prefix = "semantic_layer.select_slot_";

} // namespace

auto compose_input_status_with_precedence(std::string_view current,
                                         std::string_view input) noexcept -> std::string_view {
    if (input.empty() || (!current.empty() && input == "Compose navigation canceled.")) {
        return current;
    }
    return input;
}

auto parse_application_command(std::string_view id) noexcept
    -> std::optional<application_command_t> {
    constexpr std::pair<std::string_view, unsigned> note_kinds[]{
        {"tap", 0}, {"hold", 1}, {"mine", 4}, {"invisible", 3},
        {"lift", 5}, {"fake", 6}, {"roll", 2}};
    constexpr std::string_view note_kind_prefix = "edit.note_kind_";
    if (id.starts_with(note_kind_prefix)) {
        for (const auto& [name, kind] : note_kinds)
            if (id.substr(note_kind_prefix.size()) == name)
                return application_command_t{.kind = application_command_kind_t::edit_note_kind,
                    .note_kind = kind};
        return std::nullopt;
    }
    for (const auto& command : command_names) {
        if (command.id == id) {
            return application_command_t{.kind = command.kind, .semantic_layer_slot = std::nullopt};
        }
    }

    if (!id.starts_with(semantic_layer_slot_prefix)) {
        return std::nullopt;
    }

    const auto suffix = id.substr(semantic_layer_slot_prefix.size());
    std::size_t slot{};
    const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), slot);
    if (suffix.empty() || parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() ||
        slot == 0) {
        return std::nullopt;
    }
    return application_command_t{.kind = application_command_kind_t::semantic_layer_select_slot,
                                 .semantic_layer_slot = slot};
}

} // namespace wormedit::sys

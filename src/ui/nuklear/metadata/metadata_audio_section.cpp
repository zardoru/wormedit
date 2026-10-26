#include "ui/field/rect.h"
#include "ui/nuklear/metadata/metadata_audio_section.h"
#include "ui/nuklear/chrome/tooltip.h"

#include "ui/nuklear/audio/audio_panel.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/render/theme.h"

#include <cstring>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {

static auto metadata_audio_offset_value() -> std::optional<metadata_rating_value_t> {
    const auto parsed = worm_ui_nuklear_parse_metadata_rational(state.audio_offset_text.data(), true);
    if (!parsed) return std::nullopt;
    return metadata_rating_value_t{true, parsed->numerator, parsed->denominator};
}

static void set_metadata_audio_offset_action(wormedit::ui::workspace_action_t* action,
                                             const wormedit::ui::metadata_view_t* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart || !metadata->audio_offset_editable || metadata->timing_locked ||
        !metadata_audio_offset_value()) {
        return;
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_AUDIO_OFFSET;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id;
    const auto value = *metadata_audio_offset_value();
    const auto wire = state.rational_action_text(value.numerator, value.denominator);
    action->metadata_audio_offset_numerator_text = wire.numerator;
    action->metadata_audio_offset_denominator_text = wire.denominator;
}

void render_audio_waveform(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    const wormedit::ui::audio_slices_view_t waveform{
        .visible = true,
        .has_selected_asset = metadata.audio_waveform_has_selected_asset,
        .selected_asset_id = metadata.audio_waveform_selected_asset_id,
        .analysis_pending = metadata.audio_waveform_analysis_pending,
        .waveform_available = metadata.audio_waveform_available,
        .total_frames = metadata.audio_waveform_total_frames,
        .sample_rate = metadata.audio_waveform_sample_rate,
        .has_chart = metadata.has_chart, .chart_id = metadata.chart_id,
        .document_revision = metadata.document_revision,
        .timing_locked = metadata.timing_locked || metadata.audio_offset_locked,
        .offset_numerator = metadata.audio_offset_numerator,
        .offset_denominator = metadata.audio_offset_denominator,
        .offset_drag_active = metadata.audio_offset_drag_active,
        .offset_drag_delta_frames = metadata.audio_offset_drag_delta_frames,
        .proposed_offset_numerator = metadata.proposed_audio_offset_numerator,
        .proposed_offset_denominator = metadata.proposed_audio_offset_denominator,
    };
    const auto muted_empty_state = !metadata.audio_waveform_has_selected_asset;
    if (muted_empty_state)
        nk_style_push_color(panel.context, &panel.context->style.text.color,
                            wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
    worm_ui_nuklear_render_audio_slices(panel.context, &waveform, panel.action, false,
                                        panel.window_focused, metadata.audio_waveform_columns);
    if (muted_empty_state) nk_style_pop_color(panel.context);
}

} // namespace

void render_audio_sync_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    if (!metadata_tree_push(context, NK_TREE_TAB, "Audio sync", NK_MAXIMIZED, 1672)) return;
    if (!metadata.has_chart) {
        muted_label(context, "Select a difficulty to edit it.");
        nk_tree_pop(context);
        return;
    }
    const auto editable = metadata.audio_offset_editable && !metadata.timing_locked;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 3);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Audio offset", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.67f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.audio_offset_text.data(),
                                         state.audio_offset_text.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
        wormedit::ui::nuklear::tooltip::text(context, "Seconds. Accepts 0.125, 1/8 or 125 ms.");
    nk_layout_row_push(context, 0.05f);
    nk_label_colored(context, "s", NK_TEXT_LEFT,
                     wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
    nk_layout_row_end(context);
    const auto parsed = metadata_audio_offset_value();
    if (!parsed) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label_colored(context, "Enter a number or fraction, e.g. -0.125, 1/3 or -125 ms.",
                         NK_TEXT_LEFT, wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    }
    if (metadata.timing_locked || metadata.audio_offset_locked)
        muted_label(context, metadata.audio_offset_locked ? "Unlock audio offset in Compose to edit it."
                                                         : "Unlock timing to edit the offset.");
    const auto projected = worm_ui_nuklear_format_metadata_rational(
        {metadata.audio_offset_numerator, metadata.audio_offset_denominator});
    const auto dirty = parsed
        ? parsed->numerator != metadata.audio_offset_numerator ||
              parsed->denominator != metadata.audio_offset_denominator
        : std::strcmp(state.audio_offset_text.data(), projected.c_str()) != 0;
    const auto changed = parsed && dirty;
    bool revert = false, apply = false;
    render_footer_buttons(context, dirty && metadata.audio_offset_editable,
                          changed && editable && parsed.has_value(), "Revert", "Apply",
                          revert, apply);
    if (revert) {
        const auto text = worm_ui_nuklear_format_metadata_rational(
            {metadata.audio_offset_numerator, metadata.audio_offset_denominator});
        (void)copy_metadata_text(state.audio_offset_text.data(), state.audio_offset_text.size(),
                                 text.c_str(), text.size());
    }
    if (apply) set_metadata_audio_offset_action(panel.action, &metadata);
    render_audio_waveform(panel);
    nk_tree_pop(context);
}

namespace {

static void SDLCALL metadata_file_dialog_callback(void* userdata, const char* const* filelist,
                                                  int /*selected_filter*/) {
    std::unique_ptr<std::uint64_t> generation{static_cast<std::uint64_t*>(userdata)};
    if (!generation)
        return;
    if (!filelist) {
        const auto* error = SDL_GetError();
        worm_ui_nuklear_metadata_file_dialog_complete(
            *generation, {}, error && *error ? error : "File picker failed.", false);
    } else if (!filelist[0]) {
        worm_ui_nuklear_metadata_file_dialog_complete(*generation, {}, {}, true);
    } else {
        std::size_t length{};
        while (length <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES && filelist[0][length] != '\0')
            ++length;
        if (length > WORM_UI_COMPOSE_IMPORT_PATH_BYTES) {
            worm_ui_nuklear_metadata_file_dialog_complete(
                *generation, {}, "Selected file path exceeds the import limit.", false);
        } else {
            worm_ui_nuklear_metadata_file_dialog_complete(
                *generation, std::string{filelist[0], length}, {}, false);
        }
    }
}

static void metadata_register_drop_target(struct nk_context* context,
                                          wormedit::ui::metadata_drop_target_t target) {
    if (target.destination == WORM_UI_METADATA_DROP_NONE)
        return;
    const auto widget = nk_widget_bounds(context);
    const auto clip = nk_window_get_content_region(context);
    const auto overlap = wormedit::ui::rect_intersection(
        {widget.x, widget.y, widget.w, widget.h}, {clip.x, clip.y, clip.w, clip.h});
    worm_ui_nuklear_set_metadata_drop_target(target, {.x = overlap.x,
                                                      .y = overlap.y,
                                                      .width = overlap.w,
                                                      .height = overlap.h});
}

static void metadata_read_only_path(struct nk_context* context, const char* label,
                                    const std::string& path, wormedit::ui::metadata_drop_target_t target = {}) {
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    nk_layout_row_dynamic(context, 25, 1);
    metadata_register_drop_target(context, target);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | static_cast<nk_flags>(NK_EDIT_READ_ONLY),
        buffer.data(), buffer.size(), nk_filter_default);
}

// Requests setting or replacing the song audio; false when the action slot is busy.
static auto set_metadata_music_import_action(wormedit::ui::workspace_action_t* action,
                                             const wormedit::ui::metadata_view_t* metadata, const char* path)
    -> bool {
    if (!metadata->has_chart || !path || !path[0] ||
        std::strlen(path) > WORM_UI_COMPOSE_IMPORT_PATH_BYTES ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        action->metadata_import_copy_project || action->metadata_import_copy_export ||
        metadata->import_copy_pending)
        return false;
    action->compose_import_path = path;
    action->compose_resource_role = wormedit::resource_role_t::music;
    action->compose_input_chart_id.value = metadata->chart_id.value;
    action->compose_input_document_revision = metadata->document_revision;
    action->compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
    return true;
}

static void metadata_copy_inspection_button(struct nk_context* context,
                                            const wormedit::ui::metadata_view_t* metadata,
                                            wormedit::ui::workspace_action_t* action, bool export_copy,
                                            size_t group_index) {
    nk_layout_row_dynamic(context, 25, 1);
    if (metadata->copy_preview_pending)
        nk_widget_disable_begin(context);
    const auto inspect = nk_button_label(context, export_copy ? "Inspect export destination"
                                                              : "Inspect project destination");
    if (metadata->copy_preview_pending)
        nk_widget_disable_end(context);
    if (inspect && !metadata->copy_preview_pending && !action->metadata_inspect_copy &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->metadata_inspect_copy = true;
        action->metadata_inspect_export = export_copy;
        action->compose_resource_asset_id.value = metadata->selected_music_asset_id.value;
        action->compose_output_group_index = group_index;
        action->compose_input_chart_id.value = metadata->chart_id.value;
        action->compose_input_document_revision = metadata->document_revision;
    }
    if (metadata->copy_preview_text.c_str() && !metadata->copy_preview_import &&
        metadata->copy_preview_asset_id.value == metadata->selected_music_asset_id.value &&
        metadata->copy_preview_export == export_copy &&
        (!export_copy || metadata->copy_preview_group_index == group_index)) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, metadata->copy_preview_text.c_str(), NK_TEXT_LEFT);
    }
}

static auto external_copy_allowed(const MetadataPanel& panel, const char* source_path,
                                 const wormedit::ui::compose_output_group_view_t* export_group) -> bool {
    const auto& metadata = *panel.metadata;
    if (!source_path || !source_path[0] || !metadata.project_directory.c_str() ||
        !metadata.project_directory.c_str()[0]) return false;
    if (export_group && !export_group->relative_asset_directory[0]) return false;
    const std::filesystem::path source{source_path};
    return metadata.has_chart && metadata.next_import_asset_id.value != 0 &&
           !metadata.import_copy_pending && source.is_absolute() &&
           std::strlen(source_path) <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES;
}

static void render_external_copy_destination(const MetadataPanel& panel,
                                             const std::filesystem::path& source,
                                             const wormedit::ui::compose_output_group_view_t* export_group) {
    const auto& metadata = *panel.metadata;
    if (metadata.next_import_asset_id.value == 0 || !source.is_absolute()) return;
    const auto directory = export_group
        ? std::filesystem::path{export_group->relative_asset_directory}
        : std::filesystem::path{"assets"};
    const auto relative = directory / (std::to_string(metadata.next_import_asset_id.value) + "-" +
                                       source.filename().string());
    metadata_read_only_path(panel.context,
        export_group ? "Import-copy export destination" : "Import-copy project reference",
        relative.generic_string());
}

static void request_external_copy_inspection(const MetadataPanel& panel,
                                             const char* source_path, bool export_copy,
                                             std::size_t group_index, bool can_copy) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    nk_layout_row_dynamic(panel.context, 25, 1);
    const auto disabled = !can_copy || metadata.copy_preview_pending;
    if (disabled) nk_widget_disable_begin(panel.context);
    const auto inspect = nk_button_label(panel.context,
        export_copy ? "Inspect external export copy" : "Inspect external project copy");
    if (disabled) nk_widget_disable_end(panel.context);
    if (!inspect || disabled || action->metadata_inspect_copy ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_import_path = source_path;
    action->metadata_inspect_copy = true;
    action->metadata_inspect_import = true;
    action->metadata_inspect_export = export_copy;
    action->compose_resource_asset_id.value = metadata.next_import_asset_id.value;
    action->compose_output_group_index = group_index;
    action->compose_input_chart_id.value = metadata.chart_id.value;
    action->compose_input_document_revision = metadata.document_revision;
}

static auto external_copy_preview_matches(const wormedit::ui::metadata_view_t& metadata,
                                          const std::filesystem::path& source,
                                          bool export_copy, std::size_t group_index) -> bool {
    return metadata.copy_preview_text.c_str() && metadata.copy_preview_import &&
        metadata.copy_preview_asset_id.value == metadata.next_import_asset_id.value &&
        metadata.copy_preview_export == export_copy &&
        (!export_copy || metadata.copy_preview_group_index == group_index) &&
        metadata.copy_preview_source.c_str() &&
        std::filesystem::path{metadata.copy_preview_source.c_str()} == source.lexically_normal();
}

static void render_external_copy_preview(const MetadataPanel& panel,
                                         const std::filesystem::path& source,
                                         bool export_copy, std::size_t group_index) {
    if (!external_copy_preview_matches(*panel.metadata, source, export_copy, group_index)) return;
    nk_layout_row_dynamic(panel.context, 19, 1);
    nk_label(panel.context, panel.metadata->copy_preview_text.c_str(), NK_TEXT_LEFT);
}

static void request_external_copy(const MetadataPanel& panel, const char* source_path,
                                  const wormedit::ui::compose_output_group_view_t* export_group,
                                  bool can_copy) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    const auto export_copy = export_group != nullptr;
    const auto group_index = export_copy ? export_group->document_index : 0;
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!can_copy) nk_widget_disable_begin(panel.context);
    const auto* label = metadata.import_copy_pending ? "Copying and validating audio"
        : export_copy ? "Copy and link audio into export" : "Copy and link audio into project";
    const auto copy = nk_button_label(panel.context, label);
    if (!can_copy) nk_widget_disable_end(panel.context);
    if (!copy || !can_copy || action->metadata_import_copy_project ||
        action->metadata_import_copy_export || action->metadata_inspect_copy ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_import_path = source_path;
    action->compose_input_chart_id.value = metadata.chart_id.value;
    action->compose_input_document_revision = metadata.document_revision;
    action->compose_resource_asset_id.value = metadata.next_import_asset_id.value;
    if (export_copy) {
        action->compose_output_group_index = group_index;
        action->metadata_import_copy_export = true;
    } else {
        action->metadata_import_copy_project = true;
    }
}

static void
metadata_external_copy_button(struct nk_context* context, const wormedit::ui::metadata_view_t* metadata,
                              wormedit::ui::workspace_action_t* action, const char* source_path,
                              const wormedit::ui::compose_output_group_view_t* export_group = nullptr) {
    if (!source_path || !source_path[0] || !metadata->project_directory.c_str() ||
        !metadata->project_directory.c_str()[0] ||
        (export_group && !export_group->relative_asset_directory[0])) return;
    const MetadataPanel panel{context, metadata, action, false};
    const auto source = std::filesystem::path{source_path};
    const bool export_copy = export_group != nullptr;
    const auto can_copy = external_copy_allowed(panel, source_path, export_group);
    render_external_copy_destination(panel, source, export_group);
    const auto group_index = export_copy ? export_group->document_index : 0;
    request_external_copy_inspection(panel, source_path, export_copy, group_index, can_copy);
    render_external_copy_preview(panel, source, export_copy, group_index);
    request_external_copy(panel, source_path, export_group, can_copy);
}

static void consume_metadata_file_dialog_result() {
    auto picked = worm_ui_nuklear_metadata_file_dialog_take();
    if (!picked) return;
    state.external_music_error.fill('\0');
    if (!*picked) {
        std::snprintf(state.external_music_error.data(), state.external_music_error.size(), "%s",
                      picked->error().message.c_str());
        return;
    }
    const auto& choice = picked->value();
    if (choice.canceled) return;
    if (choice.path.size() >= state.external_music_path.size() ||
        !std::filesystem::path{choice.path}.is_absolute()) {
        std::snprintf(state.external_music_error.data(), state.external_music_error.size(),
                      "Choose a bounded absolute file path.");
        return;
    }
    std::snprintf(state.external_music_path.data(), state.external_music_path.size(), "%s",
                  choice.path.c_str());
}

static void open_metadata_file_dialog() {
    const auto generation = worm_ui_nuklear_metadata_file_dialog_begin();
    if (!generation) return;
    auto* token = new (std::nothrow) std::uint64_t{*generation};
    if (!token) {
        worm_ui_nuklear_metadata_file_dialog_complete(
            *generation, {}, "File picker could not allocate callback state.", false);
        return;
    }
    state.external_music_error.fill('\0');
    SDL_ShowOpenFileDialog(metadata_file_dialog_callback, token, SDL_GetKeyboardFocus(),
                           nullptr, 0, nullptr, false);
}

// Applies a picked file as the song audio as soon as an action slot is free.
static void apply_picked_music(const MetadataPanel& panel) {
    if (!state.external_music_path[0]) return;
    if (!panel.metadata->has_chart) {
        std::snprintf(state.external_music_error.data(), state.external_music_error.size(),
                      "Open a chart before choosing its audio.");
        state.external_music_path.fill('\0');
        return;
    }
    if (set_metadata_music_import_action(panel.action, panel.metadata,
                                         state.external_music_path.data()))
        state.external_music_path.fill('\0');
}

static auto current_music_filename(const wormedit::ui::metadata_view_t& metadata) -> std::string {
    if (state.external_music_path[0])
        return std::filesystem::path{state.external_music_path.data()}.filename().string();
    if (metadata.selected_music_source.c_str() && metadata.selected_music_source.c_str()[0])
        return std::filesystem::path{metadata.selected_music_source.c_str()}.filename().string();
    return {};
}

static void render_metadata_audio_picker(const MetadataPanel& panel) {
    auto* context = panel.context;
    consume_metadata_file_dialog_result();
    apply_picked_music(panel);
    const auto dialog_open = worm_ui_nuklear_metadata_file_dialog_open();
    const auto filename = current_music_filename(*panel.metadata);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 3);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Audio file", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.13f);
    const auto disabled = dialog_open || panel.metadata->import_copy_pending;
    if (disabled) nk_widget_disable_begin(context);
    const auto browse = nk_button_label(context, dialog_open ? "Choosing..." : "Choose...");
    if (disabled) nk_widget_disable_end(context);
    if (browse && !disabled) open_metadata_file_dialog();
    nk_layout_row_push(context, 0.59f);
    if (!filename.empty())
        nk_label(context, filename.c_str(), NK_TEXT_LEFT);
    else
        nk_label_colored(context, "None", NK_TEXT_LEFT,
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
    nk_layout_row_end(context);
    if (state.external_music_error[0]) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, state.external_music_error.data(), NK_TEXT_LEFT);
    }
}

static auto find_output_group(const wormedit::ui::metadata_view_t& metadata, std::size_t index)
    -> const wormedit::ui::compose_output_group_view_t* {
    for (std::size_t i = 0; i < metadata.output_groups.size(); ++i)
        if (metadata.output_groups[i].document_index == index) return &metadata.output_groups[i];
    return nullptr;
}

static auto find_dropped_music_group(const wormedit::ui::metadata_view_t& metadata)
    -> const wormedit::ui::compose_output_group_view_t* {
    if (metadata.dropped_music_target.destination != WORM_UI_METADATA_DROP_EXPORT) return nullptr;
    return find_output_group(metadata, metadata.dropped_music_target.output_group_index);
}

static void render_dropped_music_target_directory(
    const MetadataPanel& panel, const wormedit::ui::compose_output_group_view_t* dropped_group) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    const auto destination = metadata.dropped_music_target.destination;
    if (destination == WORM_UI_METADATA_DROP_PROJECT) {
        muted_label(context, "Drop target: project assets; different files are preserved.");
        if (!metadata.project_directory.c_str() || !metadata.project_directory.c_str()[0]) {
            muted_label(context, "Open a saved native project before copying a drop.");
        } else {
            metadata_read_only_path(context, "Drop target directory",
                (std::filesystem::path{metadata.project_directory.c_str()} / "assets").generic_string());
        }
    } else if (destination == WORM_UI_METADATA_DROP_EXPORT) {
        muted_label(context, dropped_group ? "Drop target: selected export group assets."
                                            : "The dropped export group is no longer available.");
        if (dropped_group && metadata.project_directory.c_str() && metadata.project_directory.c_str()[0] &&
            dropped_group->relative_asset_directory[0])
            metadata_read_only_path(context, "Drop target directory",
                (std::filesystem::path{metadata.project_directory.c_str()} /
                 dropped_group->relative_asset_directory).lexically_normal().generic_string());
    }
}

static auto dropped_music_is_selected_asset(const wormedit::ui::metadata_view_t& metadata,
                                            const std::filesystem::path& project) -> bool {
    const auto selected_source = std::filesystem::path{
        metadata.selected_music_source.c_str() ? metadata.selected_music_source.c_str() : ""};
    if (!metadata.selected_music_asset_id.value || project.empty() || selected_source.empty()) return false;
    const auto resolved = selected_source.is_absolute()
        ? selected_source.lexically_normal() : (project / selected_source).lexically_normal();
    return std::filesystem::path{metadata.dropped_music_path.c_str()}.lexically_normal() == resolved;
}

static void render_dropped_selected_music_copy(
    const MetadataPanel& panel, const wormedit::ui::compose_output_group_view_t* group,
    const std::filesystem::path& project) {
    const auto& metadata = *panel.metadata;
    const auto destination = metadata.dropped_music_target.destination;
    const auto relative_directory = destination == WORM_UI_METADATA_DROP_PROJECT
        ? std::filesystem::path{"assets"}
        : std::filesystem::path{group->relative_asset_directory};
    const auto selected_source = std::filesystem::path{metadata.selected_music_source.c_str()};
    const auto filename = std::to_string(metadata.selected_music_asset_id.value) + "-" +
                         selected_source.filename().string();
    metadata_read_only_path(panel.context, "Normalized project-relative destination",
                            (relative_directory / filename).generic_string());
    metadata_copy_inspection_button(panel.context, &metadata, panel.action,
                                    destination == WORM_UI_METADATA_DROP_EXPORT,
                                    group ? group->document_index : 0);
    nk_layout_row_dynamic(panel.context, 25, 1);
    const auto label = destination == WORM_UI_METADATA_DROP_PROJECT
        ? "Confirm drop: copy selected music to project"
        : "Confirm drop: copy selected music to export";
    const auto clicked = nk_button_label(panel.context, label);
    if (!clicked || panel.action->metadata_inspect_copy ||
        panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE || !metadata.has_chart) return;
    auto* action = panel.action;
    action->compose_resource_asset_id.value = metadata.selected_music_asset_id.value;
    action->compose_input_chart_id.value = metadata.chart_id.value;
    action->compose_input_document_revision = metadata.document_revision;
    if (destination == WORM_UI_METADATA_DROP_PROJECT)
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    else {
        action->compose_output_group_index = group->document_index;
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    }
    (void)project;
}

static void render_dropped_music_link(const MetadataPanel& panel,
                                      const wormedit::ui::compose_output_group_view_t* group) {
    const auto& metadata = *panel.metadata;
    const auto can_link = metadata.has_chart && !metadata.import_copy_pending &&
        std::strlen(metadata.dropped_music_path.c_str()) <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES;
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!can_link) nk_widget_disable_begin(panel.context);
    const auto link = nk_button_label(panel.context, "Use as song audio");
    if (!can_link) nk_widget_disable_end(panel.context);
    if (link && can_link)
        (void)set_metadata_music_import_action(panel.action, &metadata,
                                               metadata.dropped_music_path.c_str());
    const auto destination = metadata.dropped_music_target.destination;
    if (destination == WORM_UI_METADATA_DROP_PROJECT)
        metadata_external_copy_button(panel.context, &metadata, panel.action,
                                      metadata.dropped_music_path.c_str());
    else if (destination == WORM_UI_METADATA_DROP_EXPORT && group)
        metadata_external_copy_button(panel.context, &metadata, panel.action,
                                      metadata.dropped_music_path.c_str(), group);
}

static void render_dropped_music(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (!metadata.dropped_music_path.c_str() || !metadata.dropped_music_path.c_str()[0]) return;
    metadata_read_only_path(panel.context, "Dropped audio file", metadata.dropped_music_path.c_str());
    const auto* group = find_dropped_music_group(metadata);
    render_dropped_music_target_directory(panel, group);
    const auto project = std::filesystem::path{
        metadata.project_directory.c_str() ? metadata.project_directory.c_str() : ""};
    const auto copy_target_valid = metadata.dropped_music_target.destination ==
            WORM_UI_METADATA_DROP_PROJECT ||
        (group && group->relative_asset_directory[0]);
    if (copy_target_valid && dropped_music_is_selected_asset(metadata, project))
        render_dropped_selected_music_copy(panel, group, project);
    else
        render_dropped_music_link(panel, group);
}

static auto render_output_group_combo(const MetadataPanel& panel)
    -> const wormedit::ui::compose_output_group_view_t* {
    const auto& metadata = *panel.metadata;
    if (metadata.output_groups.size() == 0) return nullptr;
    auto* selected = find_output_group(metadata, state.selected_output_group_index);
    if (!selected) {
        selected = &metadata.output_groups[0];
        state.selected_output_group_index = selected->document_index;
    }
    nk_layout_row_begin(panel.context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(panel.context, 0.28f);
    nk_label(panel.context, "Export format", NK_TEXT_LEFT);
    nk_layout_row_push(panel.context, 0.72f);
    const auto* label = selected->name[0] ? selected->name.c_str() : selected->relative_output_path.c_str();
    if (wormedit::ui::chrome::safe_combo_begin_label(panel.context, label,
                             nk_vec2(nk_widget_width(panel.context), 180.0f))) {
        nk_layout_row_dynamic(panel.context, 22, 1);
        for (size_t index = 0; index < metadata.output_groups.size(); ++index) {
            const auto& group = metadata.output_groups[index];
            const auto* group_label = group.name[0] ? group.name.c_str() : group.relative_output_path.c_str();
            if (!nk_combo_item_label(panel.context, group_label, NK_TEXT_LEFT)) continue;
            selected = &group;
            state.selected_output_group_index = group.document_index;
        }
        wormedit::ui::chrome::safe_combo_end(panel.context);
    }
    nk_layout_row_end(panel.context);
    return selected;
}

static void synchronize_output_path_buffer(const wormedit::ui::metadata_view_t& metadata,
                                          const wormedit::ui::compose_output_group_view_t& selected) {
    if (state.output_path_buffer_revision == metadata.document_revision &&
        state.output_path_buffer_group_index == selected.document_index &&
        std::strcmp(state.output_path_source.data(), selected.relative_output_path.c_str()) == 0) return;
    state.output_path_buffer_revision = metadata.document_revision;
    state.output_path_buffer_group_index = selected.document_index;
    state.output_path_buffer_valid = copy_metadata_text(
        state.output_path.data(), state.output_path.size(), selected.relative_output_path.c_str(),
        std::strlen(selected.relative_output_path.c_str()));
    state.output_path_buffer_valid = copy_metadata_text(
        state.output_path_source.data(), state.output_path_source.size(),
        selected.relative_output_path.c_str(), std::strlen(selected.relative_output_path.c_str())) &&
        state.output_path_buffer_valid;
}

static void apply_output_path_action(const MetadataPanel& panel,
                                     const wormedit::ui::compose_output_group_view_t& selected,
                                     bool changed) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    if (!changed || !state.output_path_buffer_valid || state.output_path[0] == '\0') return;
    if (action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_OUTPUT_PATH;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_output_group_index = selected.document_index;
    action->metadata_output_path = state.output_path.data();
}

static void render_output_path_editor(const MetadataPanel& panel,
                                     const wormedit::ui::compose_output_group_view_t& selected) {
    auto* context = panel.context;
    synchronize_output_path_buffer(*panel.metadata, selected);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Export folder", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    metadata_register_drop_target(context,
        {WORM_UI_METADATA_DROP_OUTPUT_PATH, selected.document_index});
    if (!state.output_path_buffer_valid) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.output_path.data(),
                                         state.output_path.size(), nk_filter_default);
    if (!state.output_path_buffer_valid) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    const auto changed = state.output_path_buffer_valid &&
        std::strcmp(state.output_path.data(), selected.relative_output_path.c_str()) != 0;
    const auto apply_enabled = changed && state.output_path[0] != '\0';
    nk_layout_row_template_begin(context, 23);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 96);
    nk_layout_row_template_push_static(context, 96);
    nk_layout_row_template_end(context);
    nk_spacing(context, 1);
    auto revert_style = wormedit::ui::chrome::button_style(context, false);
    style_metadata_footer_button(revert_style, changed, false);
    const auto revert = nk_button_label_styled(context, &revert_style, "Revert");
    auto apply_style = wormedit::ui::chrome::button_style(context, apply_enabled);
    style_metadata_footer_button(apply_style, apply_enabled, true);
    const auto apply = nk_button_label_styled(context, &apply_style, "Apply");
    if (revert && changed)
        (void)copy_metadata_text(state.output_path.data(), state.output_path.size(),
                                 selected.relative_output_path.c_str(),
                                 std::strlen(selected.relative_output_path.c_str()));
    if (apply && apply_enabled) apply_output_path_action(panel, selected, changed);
}

static void render_dropped_output_path(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (!metadata.dropped_output_relative_path.c_str() || !metadata.dropped_output_relative_path.c_str()[0]) return;
    const auto* group = find_output_group(metadata, metadata.dropped_output_group_index);
    metadata_read_only_path(panel.context, "Dropped output path",
                            metadata.dropped_output_relative_path.c_str());
    muted_label(panel.context, group
        ? "Apply this path to the output group under the drop target."
        : "The dropped output group is no longer available.");
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!group) nk_widget_disable_begin(panel.context);
    const auto apply = nk_button_label(panel.context, "Apply dropped output path");
    if (!group) nk_widget_disable_end(panel.context);
    if (!apply || !group || panel.action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    panel.action->metadata_edit = WORM_UI_METADATA_EDIT_OUTPUT_PATH;
    panel.action->metadata_document_revision = metadata.document_revision;
    panel.action->metadata_output_group_index = group->document_index;
    panel.action->metadata_output_path = metadata.dropped_output_relative_path.c_str();
}

struct MusicCopyInfo {
    std::filesystem::path project;
    std::string destination_name;
    bool has_music{};
};

static auto render_project_copy_info(const MetadataPanel& panel) -> std::optional<MusicCopyInfo> {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    if (!metadata.project_directory.c_str() || !metadata.project_directory.c_str()[0]) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label_colored(context, "Save the project to copy audio files.", NK_TEXT_LEFT,
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
        return std::nullopt;
    }
    MusicCopyInfo info{.project = std::filesystem::path{metadata.project_directory.c_str()},
                       .destination_name = {}, .has_music = false};
    metadata_read_only_path(context, "Project directory", info.project.generic_string(),
                            {WORM_UI_METADATA_DROP_PROJECT, 0});
    info.has_music = metadata.selected_music_asset_id.value && metadata.selected_music_source.c_str();
    if (!info.has_music) {
        muted_label(context, "Select a music asset for project or export copies.");
        return info;
    }
    const std::filesystem::path source{metadata.selected_music_source.c_str()};
    metadata_read_only_path(context, "Selected music source",
        source.is_absolute() ? source.generic_string()
                             : (info.project / source).lexically_normal().generic_string());
    char references[128];
    (void)std::snprintf(references, sizeof(references),
                        "Asset references: %zu catalog resources, %zu sound channels.",
                        metadata.selected_music_resource_count, metadata.selected_music_channel_count);
    muted_label(context, references);
    info.destination_name = std::to_string(metadata.selected_music_asset_id.value) + "-" +
                            source.filename().string();
    metadata_read_only_path(context, "Project copy destination",
                            (info.project / "assets" / info.destination_name).generic_string());
    metadata_copy_inspection_button(context, &metadata, panel.action, false, 0);
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, "Copy music into project assets") &&
        !panel.action->metadata_inspect_copy &&
        panel.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        panel.action->compose_resource_asset_id.value = metadata.selected_music_asset_id.value;
        panel.action->compose_input_chart_id.value = metadata.chart_id.value;
        panel.action->compose_input_document_revision = metadata.document_revision;
        panel.action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    }
    return info;
}

static void render_export_copy_info(const MetadataPanel& panel,
                                    const wormedit::ui::compose_output_group_view_t& selected,
                                    const MusicCopyInfo& info) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    if (selected.relative_asset_directory[0] == '\0') {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "The output group has no safe asset directory.", NK_TEXT_LEFT);
        return;
    }
    metadata_read_only_path(context, "Output group directory",
        (info.project / std::filesystem::path{selected.relative_asset_directory.c_str()}.parent_path())
            .lexically_normal().generic_string(),
        {WORM_UI_METADATA_DROP_EXPORT, selected.document_index});
    if (!info.has_music) return;
    metadata_read_only_path(context, "Export copy destination",
        (info.project / selected.relative_asset_directory.c_str() / info.destination_name)
            .lexically_normal().generic_string());
    metadata_copy_inspection_button(context, &metadata, panel.action, true,
                                    selected.document_index);
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, "Copy music into selected export folder") &&
        !panel.action->metadata_inspect_copy &&
        panel.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        panel.action->compose_resource_asset_id.value = metadata.selected_music_asset_id.value;
        panel.action->compose_output_group_index = selected.document_index;
        panel.action->compose_input_chart_id.value = metadata.chart_id.value;
        panel.action->compose_input_document_revision = metadata.document_revision;
        panel.action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    }
}

static void render_project_and_export_copies(const MetadataPanel& panel,
                                             const wormedit::ui::compose_output_group_view_t* selected) {
    const auto info = render_project_copy_info(panel);
    if (!info) return;
    if (!selected) {
        muted_label(panel.context, "No output group includes this difficulty.");
        return;
    }
    render_export_copy_info(panel, *selected, *info);
}

} // namespace

void render_metadata_paths(const MetadataPanel& panel) {
    auto* context = panel.context;
    worm_ui_nuklear_clear_metadata_drop_targets();
    if (!metadata_tree_push(context, NK_TREE_TAB, "Audio and export", NK_MAXIMIZED, 1728)) return;
    render_metadata_audio_picker(panel);
    muted_label(context, "Drop audio here to link it, or on a folder to copy it.");
    render_dropped_music(panel);
    const auto* selected = render_output_group_combo(panel);
    if (selected) render_output_path_editor(panel, *selected);
    render_dropped_output_path(panel);
    render_project_and_export_copies(panel, selected);
    nk_tree_pop(context);
}

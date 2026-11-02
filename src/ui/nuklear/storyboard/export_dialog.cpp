#include "ui/nuklear/storyboard/export_dialog.h"
#include "ui/nuklear/chrome/dialog.h"
#include "ui/nuklear/chrome/file_dialog_slot.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace wormedit::ui {
namespace {
struct video_dialog_t {
    chrome::Dialog dialog;
    file_dialog_slot_t picker;
    song_id_t project;
    std::uint64_t session{}, generation{};
    std::array<char, 4097> filename{};
    std::string error;
    bool bound{};
    void bind(const storyboard_view_t& view) {
        if (bound && project == view.project && session == view.session_generation && generation == view.video_export.generation) return;
        if (project != view.project || session != view.session_generation) { picker.reset(); error.clear(); }
        project = view.project; session = view.session_generation; generation = view.video_export.generation;
        std::snprintf(filename.data(), filename.size(), "%s", view.video_export.settings.filename.c_str());
        bound = true;
    }
};
video_dialog_t state;
void picked(void* data, const char* const* files, int) {
    const std::unique_ptr<std::uint64_t> token{static_cast<std::uint64_t*>(data)};
    state.picker.complete(*token, files && files[0] ? files[0] : "", files ? "" : SDL_GetError(), files && !files[0]);
}
void choose_destination(media::video_profile_t profile) {
    const auto token = state.picker.begin();
    if (!token) return;
    static constexpr SDL_DialogFileFilter mp4{"MP4 H.264 / AAC", "mp4"}, webm{"WebM VP9 / Opus", "webm"};
    SDL_ShowSaveFileDialog(picked, new std::uint64_t{*token}, SDL_GetKeyboardFocus(),
        profile == media::video_profile_t::mp4_h264_aac ? &mp4 : &webm, 1, state.filename.data());
}
void emit(const storyboard_view_t& view, storyboard_action_t& action, storyboard_video_action_kind_t kind,
    const storyboard_video_settings_t& settings) {
    action = storyboard_action_t::from(view, storyboard_action_kind_t::video_export, view.state.selection.storyboard);
    action.video_export = {.kind = kind, .generation = view.video_export.generation, .settings = settings};
}
void render_profile(nk_context* context, storyboard_video_settings_t& settings) {
    nk_layout_row_dynamic(context, 26, 2);
    const auto mp4 = settings.profile == media::video_profile_t::mp4_h264_aac;
    const auto selected = nk_option_label(context, "MP4 H.264 + AAC", mp4);
    const auto webm = nk_option_label(context, "WebM VP9 + Opus", !mp4);
    if ((selected && !mp4) || (webm && mp4)) {
        settings.profile = mp4 ? media::video_profile_t::webm_vp9_opus : media::video_profile_t::mp4_h264_aac;
        const auto dot = settings.filename.find_last_of('.');
        settings.filename = settings.filename.substr(0, dot) + (mp4 ? ".webm" : ".mp4");
        std::snprintf(state.filename.data(), state.filename.size(), "%s", settings.filename.c_str());
    }
}
void render_resolution(nk_context* context, const storyboard_video_export_view_t& view, storyboard_video_settings_t& settings) {
    nk_layout_row_dynamic(context, 26, 4);
    static constexpr std::array sizes{std::pair{640U,480U}, std::pair{1280U,720U}, std::pair{1920U,1080U}, std::pair{3840U,2160U}};
    for (const auto& [width,height] : sizes) {
        const auto capability = std::ranges::find(view.capabilities, width, &storyboard_video_capability_t::width);
        const bool available = capability != view.capabilities.end() && capability->available;
        // A failed disk/path preflight must not trap a retained UHD setting.
        // Lower resolutions may be selected to request a new real probe;
        // Start still requires that successful matching result.
        const bool selectable = available || (height != 2160 && capability == view.capabilities.end() && view.available);
        char label[32]{}; std::snprintf(label, sizeof(label), "%u×%u", width, height);
        if (!selectable) nk_widget_disable_begin(context);
        if (nk_option_label(context, label, settings.width == width) && selectable) { settings.width = width; settings.height = height; }
        if (!selectable) nk_widget_disable_end(context);
    }
    for (const auto& capability : view.capabilities) {
        if (capability.available) continue;
        nk_layout_row_dynamic(context, 32, 1);
        nk_labelf_wrap(context, "%u×%u unavailable: %s", capability.width, capability.height, capability.reason.c_str());
    }
}
void render_choices(nk_context* context, const storyboard_video_export_view_t& view, storyboard_video_settings_t& settings) {
    nk_layout_row_dynamic(context, 26, 2);
    if (nk_option_label(context, "30 fps", settings.fps == 30)) settings.fps = 30;
    if (nk_option_label(context, "60 fps", settings.fps == 60)) settings.fps = 60;
    nk_layout_row_dynamic(context, 26, 2);
    if (nk_option_label(context, "Whole song", !settings.selection)) settings.selection = false;
    const bool selection = view.selection && view.selection->is_playable();
    if (!selection) nk_widget_disable_begin(context);
    if (nk_option_label(context, "Timeline selection", settings.selection) && selection) settings.selection = true;
    if (!selection) nk_widget_disable_end(context);
    nk_layout_row_dynamic(context, 22, 1);
    if (selection) nk_labelf(context, NK_TEXT_LEFT, "Timeline source range: %.3f–%.3f s",
        static_cast<double>(view.selection->begin_ms) / 1000, static_cast<double>(view.selection->end_ms) / 1000);
    else nk_label(context, "Timeline range needs keys selected at two different source times.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 26, 3);
    if (nk_option_label(context, "No audio", settings.audio == storyboard_video_audio_t::none)) settings.audio = storyboard_video_audio_t::none;
    if (nk_option_label(context, "Song audio", settings.audio == storyboard_video_audio_t::song)) settings.audio = storyboard_video_audio_t::song;
    if (nk_option_label(context, "Song + chart samples", settings.audio == storyboard_video_audio_t::song_and_samples)) settings.audio = storyboard_video_audio_t::song_and_samples;
    nk_layout_row_dynamic(context, 26, 3);
    if (nk_option_label(context, "Pass", settings.layers == storyboard_conditional_layer_t::pass)) settings.layers = storyboard_conditional_layer_t::pass;
    if (nk_option_label(context, "Fail", settings.layers == storyboard_conditional_layer_t::fail)) settings.layers = storyboard_conditional_layer_t::fail;
    if (nk_option_label(context, "Neither", settings.layers == storyboard_conditional_layer_t::neither)) settings.layers = storyboard_conditional_layer_t::neither;
}
void render_destination(nk_context* context, storyboard_video_settings_t& settings) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Destination (relative to saved project, or absolute)", NK_TEXT_LEFT);
    nk_layout_row_template_begin(context, 28); nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 100); nk_layout_row_template_end(context);
    chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.filename.data(), state.filename.size(), nk_filter_default);
    if (nk_button_label(context, "Browse…")) choose_destination(settings.profile);
    if (const auto choice = state.picker.take()) {
        if (!*choice) state.error = choice->error().message;
        else if (!choice->value().canceled) std::snprintf(state.filename.data(), state.filename.size(), "%s", choice->value().path.c_str());
    }
    settings.filename = state.filename.data();
    nk_layout_row_dynamic(context, 24, 1);
    nk_bool replace = settings.replace_existing;
    nk_checkbox_label(context, "Replace an existing file", &replace);
    settings.replace_existing = replace != 0;
}
void render_details(nk_context* context, const storyboard_video_export_view_t& view) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, view.probing ? "Checking encoder support…" : view.status.c_str(), NK_TEXT_LEFT);
    for (const auto& reason : view.blockers) { nk_layout_row_dynamic(context, 32, 1); nk_label_wrap(context, reason.c_str()); }
    if (!state.error.empty()) { nk_layout_row_dynamic(context, 24, 1); nk_label_wrap(context, state.error.c_str()); }
    if (view.estimated_bytes) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Approx. %.1f MiB; render %.0f s; %.1f GiB free", static_cast<double>(view.estimated_bytes) / 1048576,
            view.estimated_seconds, static_cast<double>(view.free_bytes) / 1073741824);
    }
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Size and render time are approximate and vary with the scene; encoding adds time. Both tracks use source-song time. The final frame adds less than one frame of silence.");
    if (view.source_clock_with_cuts) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, "This project has export cuts. Video and audio retain the same source-song interval; cut splices are not applied to this export.");
    }
}
}
void reset_storyboard_video_dialog() {
    if (!state.bound && !state.picker.open()) return;
    state.picker.reset(); state.bound = false; state.error.clear();
}
void render_storyboard_video_dialog(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action) {
    const auto& video = view.video_export;
    if (!video.open) { reset_storyboard_video_dialog(); return; }
    state.bind(view);
    auto settings = video.settings;
    if (chrome::dialog_begin(state.dialog, context, "storyboard-video-export", "Export video", {.width = 700, .enter_confirms = false})) {
        if (video.running) {
            nk_layout_row_dynamic(context, 24, 1);
            nk_label(context, "An export is running. Close this dialog to keep editing, or cancel the job.", NK_TEXT_LEFT);
        } else {
            render_profile(context, settings); render_resolution(context, video, settings);
            render_choices(context, video, settings); render_destination(context, settings); render_details(context, video);
        }
        chrome::dialog_footer_begin(state.dialog, context, 2);
        (void)chrome::dialog_button(state.dialog, context, "Close", chrome::DialogButtonRole::secondary);
        (void)chrome::dialog_button(state.dialog, context, video.running ? "Cancel export" : "Export video",
            chrome::DialogButtonRole::primary, video.running ? !video.cancelling : video.can_start && settings == video.settings);
    }
    const auto result = chrome::dialog_end(state.dialog, context);
    if (result == chrome::DialogResult::cancel || result == chrome::DialogResult::secondary)
        emit(view, action, storyboard_video_action_kind_t::close, settings);
    else if (result == chrome::DialogResult::primary)
        emit(view, action, video.running ? storyboard_video_action_kind_t::cancel : storyboard_video_action_kind_t::start, settings);
    else if (settings != video.settings) emit(view, action, storyboard_video_action_kind_t::configure, settings);
}
}

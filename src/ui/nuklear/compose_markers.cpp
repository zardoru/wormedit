#include "ui/nuklear/chrome/text_fit.h"
#include "ui/nuklear/compose_menu_command.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_note_label_inline.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/render/lucide_icons.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/render/vector_icons.h"

#include <SDL3/SDL_timer.h>

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace {

struct compose_marker_state_t {
    std::uint64_t project_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t chart_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t document_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t bookmark_id{};
    std::uint64_t section_id{};
    std::uint64_t audio_bookmark_id{};
    std::uint64_t audio_section_id{};
    std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1> audio_bookmark_label{};
    std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1> audio_section_label{};
    std::array<char, WORM_UI_COMPOSE_MARKER_TIME_BYTES> audio_bookmark_time{};
    std::array<char, WORM_UI_COMPOSE_MARKER_TIME_BYTES> audio_section_begin{};
    std::array<char, WORM_UI_COMPOSE_MARKER_TIME_BYTES> audio_section_end{};
};

compose_marker_state_t state;

void copy_label(std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1>& destination,
                const char* source) {
    destination.fill('\0');
    if (!source) {
        return;
    }
    const auto length = std::min(std::strlen(source), destination.size() - 1);
    std::memcpy(destination.data(), source, length);
    destination[length] = '\0';
}

template <std::size_t capacity>
void copy_time(std::array<char, capacity>& destination, std::int64_t milliseconds) {
    destination.fill('\0');
    (void)std::snprintf(destination.data(), destination.size(), "%lld",
                        static_cast<long long>(milliseconds));
}

auto selected_bookmark(const wormedit::ui::compose_view_t& view) -> const wormedit::ui::compose_bookmark_view_t* {
    for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
        if (view.beat_bookmarks[index].bookmark_id.value == state.bookmark_id) {
            return &view.beat_bookmarks[index];
        }
    }
    return nullptr;
}

auto selected_section(const wormedit::ui::compose_view_t& view) -> const wormedit::ui::compose_section_view_t* {
    for (std::size_t index = 0; index < view.beat_sections.size(); ++index) {
        if (view.beat_sections[index].section_id.value == state.section_id) {
            return &view.beat_sections[index];
        }
    }
    return nullptr;
}

auto selected_audio_bookmark(const wormedit::ui::compose_view_t& view)
    -> const wormedit::ui::compose_audio_bookmark_view_t* {
    for (std::size_t index = 0; index < view.audio_bookmarks.size(); ++index) {
        if (view.audio_bookmarks[index].bookmark_id.value == state.audio_bookmark_id) {
            return &view.audio_bookmarks[index];
        }
    }
    return nullptr;
}

auto selected_audio_section(const wormedit::ui::compose_view_t& view) -> const wormedit::ui::compose_audio_section_view_t* {
    for (std::size_t index = 0; index < view.audio_sections.size(); ++index) {
        if (view.audio_sections[index].section_id.value == state.audio_section_id) {
            return &view.audio_sections[index];
        }
    }
    return nullptr;
}

void synchronize(const wormedit::ui::compose_view_t& view) {
    if (state.project_id != view.project_id.value || state.chart_id != view.chart_id.value) {
        state = {};
    }
    if (state.project_id == view.project_id.value && state.chart_id == view.chart_id.value &&
        state.document_revision == view.document_revision) {
        return;
    }
    if (!selected_bookmark(view)) state.bookmark_id = 0;
    if (!selected_section(view)) state.section_id = 0;
    if (const auto* bookmark = selected_audio_bookmark(view)) {
        copy_label(state.audio_bookmark_label, bookmark->label.data());
        copy_time(state.audio_bookmark_time, bookmark->audio_milliseconds);
    } else {
        state.audio_bookmark_id = 0;
        state.audio_bookmark_label.fill('\0');
        copy_time(state.audio_bookmark_time, 0);
    }
    if (const auto* section = selected_audio_section(view)) {
        copy_label(state.audio_section_label, section->label.data());
        copy_time(state.audio_section_begin, section->begin_audio_milliseconds);
        copy_time(state.audio_section_end, section->end_audio_milliseconds);
    } else {
        state.audio_section_id = 0;
        state.audio_section_label.fill('\0');
        copy_time(state.audio_section_begin, 0);
        copy_time(state.audio_section_end, 1'000);
    }
    state.project_id = view.project_id.value;
    state.chart_id = view.chart_id.value;
    state.document_revision = view.document_revision;
}

// ---- Shared group chrome: a title with ‹ › + delete icon buttons, then a clickable list. ----

constexpr float header_height = 26.0f;
constexpr float row_height = 22.0f;
constexpr float row_text_inset = 8.0f;
constexpr std::uint32_t double_click_milliseconds = 400;

struct header_spec_t {
    const char* title;
    const char* previous_tip;
    const char* next_tip;
    const char* add_tip;
    const char* remove_tip;
    bool navigation_enabled;
    bool add_enabled;
    bool remove_enabled;
};

struct header_clicks_t {
    bool previous{};
    bool next{};
    bool add{};
    bool remove{};
};

template <typename Button>
auto render_header_button(struct nk_context* context, bool enabled, const Button& button) -> bool {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = button.render(context);
    if (!enabled) nk_widget_disable_end(context);
    return clicked && enabled;
}

// The title starts at the row-highlight edge and the buttons end flush with the opposite edge, so
// both sides of the header keep the same margin as the list below.
auto render_group_header(struct nk_context* context, const header_spec_t& spec) -> header_clicks_t {
    namespace chrome = wormedit::ui::chrome;
    namespace icons = wormedit::ui::vector_icons;
    using wormedit::ui::lucide::Glyph;
    nk_layout_row_template_begin(context, header_height);
    nk_layout_row_template_push_dynamic(context);
    chrome::IconButton::layout_columns(context, 4, 26.0f);
    nk_layout_row_template_end(context);
    nk_label_colored(context, spec.title, NK_TEXT_LEFT,
                     wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_bright));
    header_clicks_t clicks;
    clicks.previous = render_header_button(context, spec.navigation_enabled,
        chrome::IconButton{Glyph::chevron_left, spec.previous_tip});
    clicks.next = render_header_button(context, spec.navigation_enabled,
        chrome::IconButton{Glyph::chevron_right, spec.next_tip});
    clicks.add = render_header_button(context, spec.add_enabled,
        chrome::DrawnIconButton{icons::plus, spec.add_tip});
    clicks.remove = render_header_button(context, spec.remove_enabled,
        chrome::DrawnIconButton{icons::trash, spec.remove_tip});
    return clicks;
}

struct row_spec_t {
    std::uint64_t key;  // Unique across lists, for double-click pairing.
    const char* label;
    const char* detail;
    bool selected;
    bool interactive;
};

struct row_result_t {
    bool clicked{};
    bool double_clicked{};
    struct nk_rect bounds{};
};

std::uint64_t last_click_key{};
std::uint32_t last_click_ticks{};

auto register_click(std::uint64_t key) -> bool {
    const auto now = SDL_GetTicks();
    const auto paired = last_click_key == key && now - last_click_ticks <= double_click_milliseconds;
    last_click_key = paired ? 0 : key;
    last_click_ticks = now;
    return paired;
}

void draw_row_text(struct nk_context* context, struct nk_rect bounds, const row_spec_t& spec) {
    auto* canvas = nk_window_get_canvas(context);
    const auto* font = context->style.font;
    const auto detail_width = spec.detail && font
        ? font->width(font->userdata, font->height, spec.detail, static_cast<int>(std::strlen(spec.detail)))
        : 0.0f;
    const auto text_color = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text);
    const auto muted = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted);
    const auto label_width = std::max(0.0f, bounds.w - 3.0f * row_text_inset - detail_width);
    const auto label = wormedit::ui::chrome::fit_text_with_ellipsis(font, spec.label, label_width,
        wormedit::ui::chrome::ellipsis_overflow_t::marker_bytes);
    nk_draw_text(canvas, nk_rect(bounds.x + row_text_inset, bounds.y + 3.0f, label_width, bounds.h - 3.0f),
                 label.c_str(), static_cast<int>(label.size()), font, nk_rgba(0, 0, 0, 0), text_color);
    if (!spec.detail) return;
    nk_draw_text(canvas, nk_rect(bounds.x + bounds.w - row_text_inset - detail_width, bounds.y + 3.0f,
                                 detail_width + 1.0f, bounds.h - 3.0f),
                 spec.detail, static_cast<int>(std::strlen(spec.detail)), font, nk_rgba(0, 0, 0, 0), muted);
}

auto render_list_row(struct nk_context* context, const row_spec_t& spec, bool occluded)
    -> row_result_t {
    nk_layout_row_dynamic(context, row_height, 1);
    row_result_t result;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = spec.interactive && !occluded &&
                         nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT);
    if (nk_widget(&result.bounds, context) == NK_WIDGET_INVALID) return result;
    auto* canvas = nk_window_get_canvas(context);
    using wormedit::ui::theme::UiColor;
    if (spec.selected) nk_fill_rect(canvas, result.bounds, 4.0f, wormedit::ui::theme::ui(UiColor::selected));
    else if (hovered && spec.interactive)
        nk_fill_rect(canvas, result.bounds, 4.0f, wormedit::ui::theme::ui(UiColor::surface_hover));
    draw_row_text(context, result.bounds, spec);
    if (!clicked) return result;
    result.clicked = true;
    result.double_clicked = register_click(spec.key);
    return result;
}

void render_empty_list(struct nk_context* context, const char* text) {
    nk_layout_row_dynamic(context, row_height, 1);
    nk_label_colored(context, text, NK_TEXT_LEFT,
                     wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
}

auto row_center(const row_result_t& row) -> std::pair<float, float> {
    return {row.bounds.x + row.bounds.w * 0.5f, row.bounds.y + row.bounds.h * 0.5f};
}

constexpr std::uint64_t key_bookmark = 1ULL << 62U;
constexpr std::uint64_t key_section = 2ULL << 62U;
constexpr std::uint64_t key_audio_bookmark = 3ULL << 62U;

void stamp_action(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view) {
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
}

auto raise_compose(wormedit::ui::workspace_action_t& action, WormUiComposeActionKind kind) -> bool {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    action.compose_action = kind;
    return true;
}

void request_marker_navigation(wormedit::ui::workspace_action_t& action, bool previous, bool next) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NONE) return;
    if (previous) action.kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER;
    else if (next) action.kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER;
}

// ---- Beat bookmarks ----

void handle_bookmark_row(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                         const wormedit::ui::compose_bookmark_view_t& bookmark, const row_result_t& row) {
    if (!row.clicked) return;
    state.bookmark_id = bookmark.bookmark_id.value;
    if (row.double_clicked) {
        const auto [x, y] = row_center(row);
        (void)begin_compose_bookmark_rename_anchored(worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename,
                                                     view, bookmark.bookmark_id.value, x, y);
        return;
    }
    if (!raise_compose(action, WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK)) return;
    action.compose_bookmark_id.value = bookmark.bookmark_id.value;
    stamp_action(action, view);
}

void apply_bookmark_header(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                           const header_clicks_t& clicks) {
    request_marker_navigation(action, clicks.previous, clicks.next);
    if (clicks.add) {
        emit_compose_menu_command(action, view, "markers.add_bookmark");
        state.bookmark_id = 0;
    } else if (clicks.remove && selected_bookmark(view) &&
               raise_compose(action, WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK)) {
        action.compose_bookmark_id.value = state.bookmark_id;
    }
}

void render_bookmarks(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                      wormedit::ui::workspace_action_t& action) {
    const auto* selected = selected_bookmark(view);
    const auto editable = view.scene_available && !view.input_occluded;
    const header_spec_t spec{
        "Bookmarks", "Previous marker (bookmark or section edge)",
        "Next marker (bookmark or section edge)",
        editable ? "Add a bookmark at the cursor" : "Open a Compose chart to add bookmarks",
        selected ? "Delete the selected bookmark" : "Select a bookmark to delete it",
        view.scene_available && view.beat_marker_navigation_available, editable,
        editable && selected != nullptr};
    apply_bookmark_header(view, action, render_group_header(context, spec));
    if (view.beat_bookmarks.size() == 0) render_empty_list(context, "No bookmarks");
    for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
        const auto& bookmark = view.beat_bookmarks[index];
        char position[48]{};
        std::snprintf(position, sizeof(position), "m%llu:%llu",
                      static_cast<unsigned long long>(bookmark.measure_number),
                      static_cast<unsigned long long>(bookmark.row));
        const auto row = render_list_row(context,
            {key_bookmark | bookmark.bookmark_id.value, bookmark.label.data() && bookmark.label.data()[0] ? bookmark.label.data() : "(unnamed)",
             position, bookmark.bookmark_id.value == state.bookmark_id, view.scene_available}, view.input_occluded);
        handle_bookmark_row(view, action, bookmark, row);
    }
    if (!selected) return;
    nk_layout_row_dynamic(context, 24, 1);
    if (!editable) nk_widget_disable_begin(context);
    const auto move = nk_button_label(context, "Move selected bookmark to cursor");
    if (!editable) nk_widget_disable_end(context);
    if (move && editable && raise_compose(action, WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION))
        action.compose_bookmark_id.value = selected->bookmark_id.value;
}

// ---- Beat sections ----

void format_section_range(char (&text)[64], const wormedit::ui::compose_section_view_t& section) {
    std::snprintf(text, sizeof(text), "m%llu-m%llu",
                  static_cast<unsigned long long>(section.begin_measure_number),
                  static_cast<unsigned long long>(section.end_measure_number));
}

void handle_section_row(const wormedit::ui::compose_view_t& view, const wormedit::ui::compose_section_view_t& section,
                        const row_result_t& row) {
    if (!row.clicked) return;
    state.section_id = section.section_id.value;
    if (!row.double_clicked) return;
    const auto [x, y] = row_center(row);
    (void)begin_compose_section_label_inline(worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename, view,
                                             section.section_id.value, x, y);
}

void apply_section_header(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                          const header_clicks_t& clicks) {
    request_marker_navigation(action, clicks.previous, clicks.next);
    if (clicks.add && raise_compose(action, WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION)) {
        action.compose_section_id.value = 0;
        action.compose_marker_label.clear();
        state.section_id = 0;
    } else if (clicks.remove && selected_section(view) &&
               raise_compose(action, WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION)) {
        action.compose_section_id.value = state.section_id;
    }
}

void render_section_edge_buttons(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                                 wormedit::ui::workspace_action_t& action,
                                 const wormedit::ui::compose_section_view_t& selected) {
    const auto editable = view.scene_available && !view.input_occluded;
    nk_layout_row_dynamic(context, 24, 2);
    if (!editable) nk_widget_disable_begin(context);
    const auto set_begin = nk_button_label(context, "Set start to cursor");
    const auto set_end = nk_button_label(context, "Set end to cursor");
    if (!editable) nk_widget_disable_end(context);
    if (!editable || (!set_begin && !set_end)) return;
    const auto kind = set_begin ? WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN
                                : WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END;
    if (raise_compose(action, kind)) action.compose_section_id.value = selected.section_id.value;
}

void render_sections(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                     wormedit::ui::workspace_action_t& action) {
    const auto* selected = selected_section(view);
    const auto editable = view.scene_available && !view.input_occluded;
    const header_spec_t spec{
        "Sections", "Previous marker (bookmark or section edge)",
        "Next marker (bookmark or section edge)",
        !editable ? "Open a Compose chart to add sections"
        : view.beat_section_selection_span_available ? "Add a section from the selected span"
                                                     : "Add a one-snap section at the cursor",
        selected ? "Delete the selected section" : "Select a section to delete it",
        view.scene_available && view.beat_marker_navigation_available, editable,
        editable && selected != nullptr};
    apply_section_header(view, action, render_group_header(context, spec));
    if (view.beat_sections.size() == 0) render_empty_list(context, "No sections");
    for (std::size_t index = 0; index < view.beat_sections.size(); ++index) {
        const auto& section = view.beat_sections[index];
        char range[64]{};
        format_section_range(range, section);
        const auto row = render_list_row(context,
            {key_section | section.section_id.value, section.label.data() && section.label.data()[0] ? section.label.data() : "(unnamed)",
             range, section.section_id.value == state.section_id, true}, view.input_occluded);
        handle_section_row(view, section, row);
    }
    if (selected) render_section_edge_buttons(context, view, action, *selected);
}

// ---- Audio-time markers: the same grouping, with a compact form for the typed times. ----

void request_audio_navigation(wormedit::ui::workspace_action_t& action, bool previous, bool next) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (previous) action.compose_action = WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER;
    else if (next) action.compose_action = WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER;
}

auto audio_navigation_tip(const wormedit::ui::compose_view_t& view, bool previous) -> const char* {
    if (!view.audio_marker_navigation_available) return "No audio-time bookmarks or section boundaries";
    if (!view.audio_marker_transport_available) return "Start preview once to compile its seekable timeline";
    return previous ? "Previous audio-time marker" : "Next audio-time marker";
}

void render_audio_form_fields(struct nk_context* context, bool bookmark) {
    nk_layout_row_dynamic(context, 25, 1);
    auto& label = bookmark ? state.audio_bookmark_label : state.audio_section_label;
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, label.data(),
                                                           label.size(), nk_filter_default);
    if (bookmark) {
        nk_layout_row_dynamic(context, 25, 1);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            state.audio_bookmark_time.data(), state.audio_bookmark_time.size(), nk_filter_default);
        return;
    }
    nk_layout_row_dynamic(context, 25, 2);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.audio_section_begin.data(), state.audio_section_begin.size(), nk_filter_default);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.audio_section_end.data(), state.audio_section_end.size(), nk_filter_default);
}

void fill_audio_bookmark_action(wormedit::ui::workspace_action_t& action, bool update) {
    action.compose_bookmark_id.value = state.audio_bookmark_id;
    action.compose_marker_label = state.audio_bookmark_label.data();
    action.compose_marker_audio_begin = state.audio_bookmark_time.data();
    action.compose_action = update ? WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK
                                   : WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK;
}

void fill_audio_section_action(wormedit::ui::workspace_action_t& action, bool update) {
    action.compose_section_id.value = state.audio_section_id;
    action.compose_marker_label = state.audio_section_label.data();
    action.compose_marker_audio_begin = state.audio_section_begin.data();
    action.compose_marker_audio_end = state.audio_section_end.data();
    action.compose_action = update ? WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION
                                   : WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION;
}

void render_audio_bookmarks(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                            wormedit::ui::workspace_action_t& action) {
    const auto* selected = selected_audio_bookmark(view);
    const auto editable = view.scene_available;
    const auto navigation = view.audio_marker_navigation_available && view.audio_marker_transport_available;
    const header_spec_t spec{
        "Audio-time bookmarks", audio_navigation_tip(view, true), audio_navigation_tip(view, false),
        editable ? "Add a bookmark at the typed time" : "Open a Compose chart to add bookmarks",
        selected ? "Delete the selected bookmark" : "Select a bookmark to delete it",
        view.scene_available && navigation, editable, editable && selected != nullptr};
    const auto clicks = render_group_header(context, spec);
    request_audio_navigation(action, clicks.previous, clicks.next);
    if (view.audio_bookmarks.size() == 0) render_empty_list(context, "No audio-time bookmarks");
    for (std::size_t index = 0; index < view.audio_bookmarks.size(); ++index) {
        const auto& bookmark = view.audio_bookmarks[index];
        char time[48]{};
        std::snprintf(time, sizeof(time), "%lld ms", static_cast<long long>(bookmark.audio_milliseconds));
        const auto row = render_list_row(context,
            {key_audio_bookmark | bookmark.bookmark_id.value, bookmark.label.data() && bookmark.label.data()[0] ? bookmark.label.data() : "(unnamed)",
             time, bookmark.bookmark_id.value == state.audio_bookmark_id, true}, view.input_occluded);
        if (!row.clicked) continue;
        state.audio_bookmark_id = bookmark.bookmark_id.value;
        copy_label(state.audio_bookmark_label, bookmark.label.data());
        copy_time(state.audio_bookmark_time, bookmark.audio_milliseconds);
    }
    render_audio_form_fields(context, true);
    nk_layout_row_dynamic(context, 24, 1);
    if (!editable || !selected) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply label and time to selected");
    if (!editable || !selected) nk_widget_disable_end(context);
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (clicks.add && editable) fill_audio_bookmark_action(action, false);
    else if (apply && selected) fill_audio_bookmark_action(action, true);
    else if (clicks.remove && selected) {
        action.compose_bookmark_id.value = selected->bookmark_id.value;
        action.compose_action = WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK;
    }
}

void render_audio_sections(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                           wormedit::ui::workspace_action_t& action) {
    const auto* selected = selected_audio_section(view);
    const auto editable = view.scene_available;
    const auto navigation = view.audio_marker_navigation_available && view.audio_marker_transport_available;
    const header_spec_t spec{
        "Audio-time sections", audio_navigation_tip(view, true), audio_navigation_tip(view, false),
        editable ? "Add a section from the typed times" : "Open a Compose chart to add sections",
        selected ? "Delete the selected section" : "Select a section to delete it",
        view.scene_available && navigation, editable, editable && selected != nullptr};
    const auto clicks = render_group_header(context, spec);
    request_audio_navigation(action, clicks.previous, clicks.next);
    if (view.audio_sections.size() == 0) render_empty_list(context, "No audio-time sections");
    for (std::size_t index = 0; index < view.audio_sections.size(); ++index) {
        const auto& section = view.audio_sections[index];
        char range[64]{};
        std::snprintf(range, sizeof(range), "%lld-%lld ms",
                      static_cast<long long>(section.begin_audio_milliseconds),
                      static_cast<long long>(section.end_audio_milliseconds));
        const auto row = render_list_row(context,
            {key_audio_bookmark | (1ULL << 61U) | section.section_id.value, section.label.data() && section.label.data()[0] ? section.label.data() : "(unnamed)",
             range, section.section_id.value == state.audio_section_id, true}, view.input_occluded);
        if (!row.clicked) continue;
        state.audio_section_id = section.section_id.value;
        copy_label(state.audio_section_label, section.label.data());
        copy_time(state.audio_section_begin, section.begin_audio_milliseconds);
        copy_time(state.audio_section_end, section.end_audio_milliseconds);
    }
    render_audio_form_fields(context, false);
    nk_layout_row_dynamic(context, 24, 1);
    if (!editable || !selected) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply label and times to selected");
    if (!editable || !selected) nk_widget_disable_end(context);
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (clicks.add && editable) fill_audio_section_action(action, false);
    else if (apply && selected) fill_audio_section_action(action, true);
    else if (clicks.remove && selected) {
        action.compose_section_id.value = selected->section_id.value;
        action.compose_action = WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION;
    }
}

} // namespace

void worm_ui_nuklear_reset_compose_markers(void) {
    state = {};
}

void worm_ui_nuklear_render_compose_markers(struct nk_context* context,
                                            const wormedit::ui::compose_view_t* view,
                                            wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action) {
        return;
    }
    synchronize(*view);
    render_bookmarks(context, *view, *action);
    render_sections(context, *view, *action);
    render_audio_bookmarks(context, *view, *action);
    render_audio_sections(context, *view, *action);
}

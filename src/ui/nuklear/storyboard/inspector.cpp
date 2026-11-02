#include "ui/nuklear/storyboard/inspector.h"
#include "ui/nuklear/storyboard/storyboard_panel.h"
#include "ui/nuklear/chrome/file_dialog_slot.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/modal_input.h"
#include <SDL3/SDL_events.h>
#include <wormedit/core/numeric_text.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <limits>
#include <tuple>

namespace wormedit::ui {
namespace {
struct lua_parameter_draft_t {
    std::array<char, 129> name{};
    std::array<char, 96> value{};
};
class lua_input_t {
  public:
    file_dialog_slot_t picker;
    std::optional<storyboard_action_t> captured;
    std::array<char, 4097> module{};
    std::array<char, 129> entry{};
    std::array<char, 32> seed{}, begin{}, end{};
    std::vector<lua_parameter_draft_t> parameters;
    std::string error;
    bool picking{};
    template<std::size_t Size> static void text(std::array<char, Size>& target, std::string_view value) {
        target.fill(0);
        std::copy_n(value.data(), std::min(value.size(), Size - 1), target.data());
    }
    void reset() { picker.reset(); captured.reset(); parameters.clear(); error.clear(); picking = false; }
    void bind(const storyboard_view_t& view) {
        if (!captured) return;
        if (!view.visible || captured->project != view.project || captured->revision != view.revision ||
            captured->session_generation != view.session_generation || captured->storyboard != view.state.selection.storyboard ||
            captured->prepared_generation != view.canvas.prepared_generation ||
            captured->lua_link.selected_keys != view.state.selection.keys ||
            std::ranges::find(view.state.selection.nodes, captured->lua_link.target.track.node) == view.state.selection.nodes.end()) reset();
    }
    void open(const storyboard_view_t& view, const storyboard_lua_link_view_t& link) {
        reset();
        captured = storyboard_action_t::from(view, storyboard_action_kind_t::lua_link, view.state.selection.storyboard);
        captured->lua_link = {.kind = link.target.children ? storyboard_lua_action_kind_t::fill : storyboard_lua_action_kind_t::link,
            .target = link.target, .expected_link_id = link.link_id, .form = link.form, .selected_keys = view.state.selection.keys};
        text(module, link.form.module); text(entry, link.form.entry_point);
        text(seed, std::to_string(link.form.seed)); text(begin, std::to_string(link.form.begin_ms)); text(end, std::to_string(link.form.end_ms));
        for (const auto& value : link.form.parameters) {
            auto& draft = parameters.emplace_back(); text(draft.name, value.name); text(draft.value, format_exact_rational(value.value));
        }
    }
    auto parse() -> bool {
        if (!captured) return false;
        auto& form = captured->lua_link.form;
        form.module = module.data(); form.entry_point = entry.data(); form.parameters.clear();
        if (form.module.empty() || !parse_integer(std::string_view{seed.data()}, form.seed)) {
            error = "Choose a Lua module and an unsigned integer seed."; return false;
        }
        if (!captured->lua_link.target.children && (!parse_integer(std::string_view{begin.data()}, form.begin_ms) ||
            !parse_integer(std::string_view{end.data()}, form.end_ms) || form.begin_ms >= form.end_ms)) {
            error = "Range endpoints must be ordered signed integer milliseconds."; return false;
        }
        return parse_parameters(form);
    }
  private:
    auto parse_parameters(storyboard_lua_form_t& form) -> bool {
        for (const auto& draft : parameters) {
            const auto value = parse_exact_rational(draft.value.data());
            if (!value || draft.name[0] == 0) { error = "Parameters need a name and an exact numeric value."; return false; }
            form.parameters.push_back({.name = draft.name.data(), .value = *value});
        }
        error.clear(); return true;
    }
};
lua_input_t lua_input;
void lua_controls(nk_context*, const storyboard_view_t&, storyboard_action_t&, const storyboard_lua_target_t&);

using field_t = storyboard_inspector_field_t;
using phase_t = storyboard_inspector_phase_t;
using field_key_t = std::tuple<field_t, std::string, std::size_t>;
struct field_draft_t {
    std::array<char, 1050> text{};
    std::string baseline;
    storyboard_action_t captured;
    bool active{}, deferred_capture{};
};
struct field_scrub_t {
    field_key_t key;
    storyboard_action_t captured;
    storyboard_action_t proposed;
    double initial{}, current{};
    float anchor{};
    bool started{};
};
class inspector_input_t {
  public:
    std::map<field_key_t, field_draft_t> drafts;
    std::optional<field_scrub_t> scrub;
    std::optional<storyboard_action_t> pending_cancel;
    bool escape{}, escape_captured{}, available{};
    void bind(const storyboard_view_t& view) {
        if (project_ != view.project || session_ != view.session_generation || node_ != view.inspector.node ||
            storyboard_ != view.state.selection.storyboard) reset();
        project_ = view.project; session_ = view.session_generation;
        storyboard_ = view.state.selection.storyboard; node_ = view.inspector.node;
    }
    void reset() {
        if (scrub && scrub->started) {
            pending_cancel = scrub->captured;
            pending_cancel->inspector.phase = phase_t::cancel;
        }
        drafts.clear(); scrub.reset(); escape = false; escape_captured = false; available = false;
    }
    [[nodiscard]] auto owns_keyboard() const -> bool {
        return scrub.has_value() || std::ranges::any_of(drafts, [](const auto& item) { return item.second.active; });
    }
  private:
    song_id_t project_;
    storyboard_id_t storyboard_;
    std::uint64_t session_{};
    std::string node_;
};
inspector_input_t input;

auto capture(const storyboard_view_t& view, field_t field, std::string_view op = {}) -> storyboard_action_t {
    auto action = storyboard_action_t::from(view, storyboard_action_kind_t::inspector, view.state.selection.storyboard);
    action.inspector = {.field = field, .node = view.inspector.node, .op = std::string(op),
                        .key_time_ms = view.inspector.key_time_ms.value_or(0)};
    return action;
}
auto emit(storyboard_action_t& output, storyboard_action_t action) -> bool {
    const bool replace_focus = output.kind == storyboard_action_kind_t::inspector &&
        output.inspector.phase == phase_t::focus && action.inspector.phase != phase_t::focus;
    if (output.kind != storyboard_action_kind_t::none && !replace_focus) return false;
    output = std::move(action);
    return true;
}
auto number_text(double value) -> std::string {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                     std::chars_format::general, 12);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}
auto parse_number(std::string_view text) -> std::optional<double> {
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) return {};
    return value;
}
void reset_draft(field_draft_t& draft, std::string_view text) {
    std::snprintf(draft.text.data(), draft.text.size(), "%.*s", static_cast<int>(text.size()), text.data());
    draft.baseline = draft.text.data();
    draft.active = false;
    draft.deferred_capture = false;
}
auto update_integer_scrub(storyboard_action_t& action, std::size_t component, float delta) -> bool {
    const auto steps_text = number_text(std::trunc(static_cast<double>(delta) / 6));
    const auto steps = parse_integer<std::int64_t>(steps_text.c_str());
    if (!steps) return false;
    const auto& property = action.inspector;
    if (property.field == field_t::lifetime && !property.lifetime) return false;
    const auto initial = property.field == field_t::order ? static_cast<std::int64_t>(property.order)
        : component == 0 ? property.lifetime->begin_ms : property.lifetime->end_ms;
    const auto next = offset_by_steps({.numerator = initial, .denominator = 1},
                                     {.numerator = 1, .denominator = 1}, *steps);
    if (!next) return false;
    if (property.field == field_t::order) {
        if (next->numerator < std::numeric_limits<int>::min() || next->numerator > std::numeric_limits<int>::max()) return false;
        action.inspector.order = static_cast<int>(next->numerator);
    } else if (component == 0) action.inspector.lifetime->begin_ms = next->numerator;
    else action.inspector.lifetime->end_ms = next->numerator;
    return true;
}
void cancel_scrub(storyboard_action_t& output) {
    if (input.scrub->started) {
        auto action = input.scrub->captured;
        action.inspector.phase = phase_t::cancel;
        emit(output, std::move(action));
    }
    input.scrub.reset();
}
auto make_scrub_action(field_scrub_t& scrub, float delta, bool fine, bool down)
    -> std::optional<storyboard_action_t> {
    auto action = scrub.captured;
    const auto component = std::get<2>(scrub.key);
    const bool integer = action.inspector.field == field_t::order || action.inspector.field == field_t::lifetime;
    if (integer) {
        if (!update_integer_scrub(action, component, delta)) return {};
    } else {
        scrub.current = scrub.initial + delta * (fine ? 0.01 : 0.1);
        action.inspector.value[component] = scrub.current;
    }
    action.inspector.phase = !scrub.started ? phase_t::begin : down ? phase_t::revise : phase_t::apply;
    scrub.proposed = action;
    scrub.started = true;
    return action;
}
// Draft text belongs to the widget; identity, playhead and stamp are captured
// on activation and retained across a document revision until commit/refusal.
auto edit_text(nk_context* context, storyboard_action_t& output,
    const field_key_t& key, std::string_view current, storyboard_action_t request) -> std::optional<std::string> {
    auto& draft = input.drafts[key];
    if (!draft.active) reset_draft(draft, current);
    if (draft.deferred_capture) {
        draft.captured = request;
        draft.deferred_capture = false;
    }
    const auto flags = chrome::worm_ui_nuklear_text_field(context,
        static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT,
        draft.text.data(), static_cast<int>(draft.text.size()), nk_filter_default);
    if ((flags & NK_EDIT_ACTIVATED) != 0) {
        draft.active = true;
        draft.captured = std::move(request);
        auto focus = draft.captured;
        focus.inspector.phase = phase_t::focus;
        emit(output, std::move(focus));
    }
    if (input.escape && draft.active) {
        reset_draft(draft, current);
        nk_edit_unfocus(context);
        return {};
    }
    // An earlier field can take Nuklear's active ID before this field is visited,
    // so its DEACTIVATED edge alone cannot identify every blur.
    const bool active = (flags & NK_EDIT_ACTIVE) != 0;
    if (!draft.active || ((flags & NK_EDIT_COMMITED) == 0 && active)) return {};
    draft.active = false;
    if (active) nk_edit_unfocus(context);
    if (draft.baseline == draft.text.data()) return {};
    // The caller supplies only the changed property; the captured outer action
    // remains authoritative even if the view was refreshed during text entry.
    if (!emit(output, draft.captured)) return {};
    return std::string(draft.text.data());
}
void scrub_label(nk_context* context, const field_key_t& key, const char* label,
    storyboard_action_t request, double value, storyboard_action_t& output, bool editable) {
    const auto bounds = nk_widget_bounds(context);
    nk_label(context, label, NK_TEXT_LEFT);
    if (!editable || !input.available) return;
    const auto& mouse = context->input;
    if (!input.scrub && nk_input_is_mouse_pressed(&mouse, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(&mouse, bounds))
        input.scrub = field_scrub_t{.key = key, .captured = request, .proposed = request,
            .initial = value, .current = value, .anchor = mouse.mouse.pos.x};
    if (!input.scrub || input.scrub->key != key) return;
    auto& scrub = *input.scrub;
    if (!scrub.started) { scrub.captured = request; scrub.proposed = request; scrub.initial = value; }
    if (input.escape) {
        cancel_scrub(output);
        return;
    }
    const auto delta = mouse.mouse.pos.x - scrub.anchor;
    const bool down = nk_input_is_mouse_down(&mouse, NK_BUTTON_LEFT) != 0;
    if (!scrub.started && (!down || std::abs(delta) < 4)) {
        if (!down) input.scrub.reset();
        return;
    }
    auto action = make_scrub_action(scrub, delta, nk_input_is_key_down(&mouse, NK_KEY_SHIFT) != 0, down);
    if (!action) {
        if (!down) cancel_scrub(output);
        return;
    }
    emit(output, std::move(*action));
    if (!down) input.scrub.reset();
}
void numeric_track(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output,
    const char* label, field_t field, const storyboard_numeric_track_view_t& track, std::string_view op = {}) {
    for (std::size_t component = 0; component < track.value.size(); ++component) {
        nk_layout_row_dynamic(context, 24, 2);
        auto request = capture(view, field, op);
        request.inspector.value = track.value;
        request.inspector.key = track.key;
        const field_key_t key{field, std::string(op), component};
        const auto title = std::string(label) + (track.value.size() == 1 ? "" : " " + std::to_string(component + 1));
        const bool disable = input.available && !track.editable;
        if (disable) nk_widget_disable_begin(context);
        scrub_label(context, key, title.c_str(), request, track.value[component], output, track.editable);
        const auto displayed = input.scrub && input.scrub->key == key ? input.scrub->current : track.value[component];
        if (const auto text = edit_text(context, output, key, number_text(displayed), request)) {
            if (const auto value = parse_number(*text)) output.inspector.value[component] = *value;
            else output.inspector.has_valid_value = false;
        }
        if (disable) nk_widget_disable_end(context);
    }
    if (!track.reason.empty()) {
        nk_layout_row_dynamic(context, 28, 1); nk_label_wrap(context, track.reason.c_str());
    }
    const auto kind = field == field_t::transform ? osbx_track_kind_t::transform
        : field == field_t::color ? osbx_track_kind_t::color : osbx_track_kind_t::opacity;
    lua_controls(context, view, output, {.track = {.node = view.inspector.node, .kind = kind, .op = std::string(op)}});
}
void text_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output,
                   const char* label, field_t field, std::string_view value) {
    nk_layout_row_dynamic(context, 24, 2); nk_label(context, label, NK_TEXT_LEFT);
    if (const auto text = edit_text(context, output, {field, {}, 0}, value, capture(view, field)))
        output.inspector.text = *text;
}
void image_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 24, 2); nk_label(context, "Image", NK_TEXT_LEFT);
    const auto& inspector = view.inspector;
    const auto found = std::ranges::find(inspector.images, inspector.image.value_or(""), &storyboard_choice_view_t::id);
    const char* selected = found == inspector.images.end() ? "No image" : found->label.c_str();
    if (!chrome::safe_combo_begin_label(context, selected, nk_vec2(280, 220))) return;
    nk_layout_row_dynamic(context, 24, 1);
    for (const auto& image : inspector.images) {
        if (!nk_combo_item_label(context, image.label.c_str(), NK_TEXT_LEFT)) continue;
        auto action = capture(view, field_t::image); action.inspector.text = image.id;
        emit(output, std::move(action));
    }
    chrome::safe_combo_end(context);
}
void origin_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 20, 1); nk_label(context, "Origin", NK_TEXT_LEFT);
    for (std::size_t i = 0; i < view.inspector.origins.size(); ++i) {
        if (i % 3 == 0) nk_layout_row_dynamic(context, 23, 3);
        const auto& origin = view.inspector.origins[i];
        if (!chrome::Chip{.label = origin.label.c_str(), .chosen = origin.id == view.inspector.origin}.render(context)) continue;
        auto action = capture(view, field_t::origin); action.inspector.text = origin.id;
        emit(output, std::move(action));
    }
}
void layer_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 24, 2); nk_label(context, "Layer", NK_TEXT_LEFT);
    const auto selected = osbx_layer_names[static_cast<std::size_t>(view.inspector.layer)];
    if (!chrome::safe_combo_begin_label(context, selected.data(), nk_vec2(200, 160))) return;
    nk_layout_row_dynamic(context, 24, 1);
    for (std::size_t i = 0; i < osbx_layer_names.size(); ++i) {
        if (!nk_combo_item_label(context, osbx_layer_names[i].data(), NK_TEXT_LEFT)) continue;
        auto action = capture(view, field_t::layer); action.inspector.layer = static_cast<osbx_layer_t>(i);
        emit(output, std::move(action));
    }
    chrome::safe_combo_end(context);
}
void order_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 24, 2);
    const field_key_t key{field_t::order, {}, 0};
    auto request = capture(view, field_t::order); request.inspector.order = view.inspector.order;
    scrub_label(context, key, "Order", request, 0, output, view.inspector.editable);
    const auto displayed = input.scrub && input.scrub->key == key
        ? input.scrub->proposed.inspector.order : view.inspector.order;
    if (const auto text = edit_text(context, output, {field_t::order, {}, 0},
        std::to_string(displayed), request)) {
        int value{};
        const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size()) output.inspector.order = value;
        else output.inspector.has_valid_value = false;
    }
}
void lifetime_properties(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    const auto& lifetime = view.inspector.lifetime;
    nk_layout_row_dynamic(context, 24, 2);
    nk_label(context, lifetime ? "Lifetime (inclusive ms)" : "Lifetime: unbounded", NK_TEXT_LEFT);
    if (nk_button_label(context, lifetime ? "Clear lifetime" : "Set lifetime")) {
        auto action = capture(view, field_t::lifetime);
        if (!lifetime) action.inspector.lifetime = osbx_lifetime_t{
            .begin_ms = view.inspector.key_time_ms.value_or(0), .end_ms = view.inspector.key_time_ms.value_or(0)};
        emit(output, std::move(action));
    }
    if (!lifetime) return;
    const std::array<std::pair<const char*, std::int64_t>, 2> endpoints{{
        {"Begin ms", lifetime->begin_ms}, {"End ms", lifetime->end_ms}}};
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        nk_layout_row_dynamic(context, 24, 2);
        auto action = capture(view, field_t::lifetime); action.inspector.lifetime = lifetime;
        const field_key_t key{field_t::lifetime, {}, i};
        scrub_label(context, key, endpoints[i].first, action, 0, output, view.inspector.editable);
        const auto& proposed = input.scrub && input.scrub->key == key
            ? input.scrub->proposed.inspector.lifetime : lifetime;
        const auto displayed = i == 0 ? proposed->begin_ms : proposed->end_ms;
        const auto text = edit_text(context, output, key, std::to_string(displayed), action);
        if (!text) continue;
        std::int64_t value{};
        const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
            output.inspector.has_valid_value = false; continue;
        }
        if (i == 0) output.inspector.lifetime->begin_ms = value;
        else output.inspector.lifetime->end_ms = value;
    }
}
void blend_property(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 24, 3); nk_label(context, view.inspector.blend_keyed ? "Blend ◆" : "Blend ◇", NK_TEXT_LEFT);
    const bool disable = input.available && !view.inspector.blend_editable;
    if (disable) nk_widget_disable_begin(context);
    if (chrome::Chip{.label = "Normal", .chosen = view.inspector.blend == osbx_blend_t::normal}.render(context)) {
        auto action = capture(view, field_t::blend); action.inspector.blend = osbx_blend_t::normal;
        action.inspector.key = view.inspector.blend_key; emit(output, std::move(action));
    }
    if (chrome::Chip{.label = "Additive", .chosen = view.inspector.blend == osbx_blend_t::additive}.render(context)) {
        auto action = capture(view, field_t::blend); action.inspector.blend = osbx_blend_t::additive;
        action.inspector.key = view.inspector.blend_key; emit(output, std::move(action));
    }
    if (disable) nk_widget_disable_end(context);
}
void op_buttons(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output,
                const storyboard_op_view_t& op) {
    nk_layout_row_dynamic(context, 24, 3);
    const bool disable_up = input.available && !op.can_move_up;
    const bool disable_down = input.available && !op.can_move_down;
    if (disable_up) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Move up")) {
        auto action = capture(view, field_t::move_op, op.id); action.inspector.move = -1; emit(output, std::move(action));
    }
    if (disable_up) nk_widget_disable_end(context);
    if (disable_down) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Move down")) {
        auto action = capture(view, field_t::move_op, op.id); action.inspector.move = 1; emit(output, std::move(action));
    }
    if (disable_down) nk_widget_disable_end(context);
    if (nk_button_label(context, "Remove")) emit(output, capture(view, field_t::remove_op, op.id));
}
void add_op(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output,
            const std::optional<std::string>& parent = {}) {
    nk_layout_row_dynamic(context, 24, 1);
    if (!chrome::safe_combo_begin_label(context, parent ? "+ Add pivot child" : "+ Add op", nk_vec2(240, 200))) return;
    nk_layout_row_dynamic(context, 24, 1);
    for (const auto& choice : view.inspector.transform_choices) {
        if (!nk_combo_item_label(context, choice.label.c_str(), NK_TEXT_LEFT)) continue;
        auto action = capture(view, field_t::add_op); action.inspector.op_kind = choice.kind;
        action.inspector.parent_op = parent; emit(output, std::move(action));
    }
    chrome::safe_combo_end(context);
}
void transform_stack(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    nk_layout_row_dynamic(context, 24, 1); nk_label(context, "TRANSFORM STACK: top to bottom", NK_TEXT_LEFT);
    for (const auto& op : view.inspector.ops) {
        nk_layout_row_dynamic(context, 24, 1);
        const auto title = std::string(op.depth * 2, ' ') + "≡ " + std::to_string(op.ordinal) + " " + op.label +
                           (op.track.at_key ? " ◆" : op.track.keyed ? " ◈" : " ◇");
        nk_label(context, title.c_str(), NK_TEXT_LEFT);
        numeric_track(context, view, output, op.label.c_str(), field_t::transform, op.track, op.id);
        op_buttons(context, view, output, op);
        if (op.kind == osbx_transform_kind_t::pivot) add_op(context, view, output, op.id);
    }
    add_op(context, view, output);
}
void matrix_readout(nk_context* context, const storyboard_inspector_view_t& view) {
    nk_layout_row_dynamic(context, 22, 1);
    if (!view.time.is_valid()) {
        nk_label(context, "World matrix unavailable: invalid playhead.", NK_TEXT_LEFT);
        return;
    }
    nk_labelf(context, NK_TEXT_LEFT, "World matrix at %lld + %.6f ms", static_cast<long long>(view.time.milliseconds), view.time.fraction());
    if (!view.world) { nk_layout_row_dynamic(context, 22, 1); nk_label(context, "Unavailable for this selection", NK_TEXT_LEFT); return; }
    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "[%.5g %.5g %.5g %.5g | %.5g %.5g]",
        (*view.world)[0], (*view.world)[1], (*view.world)[2], (*view.world)[3], (*view.world)[4], (*view.world)[5]);
}
void readonly_actions(nk_context* context, const storyboard_view_t& view,
                      storyboard_action_t& output, bool input_available) {
    const auto& inspector = view.inspector;
    if (!inspector.reason.empty()) { nk_layout_row_dynamic(context, 44, 1); nk_label_wrap(context, inspector.reason.c_str()); }
    if (!inspector.message.empty()) { nk_layout_row_dynamic(context, 40, 1); nk_label_wrap(context, inspector.message.c_str()); }
    if (inspector.can_convert) {
        nk_layout_row_dynamic(context, 26, 1);
        if (!input_available) nk_widget_disable_begin(context);
        if (nk_button_label(context, "Convert to OSBX")) emit(output, capture(view, field_t::convert));
        if (!input_available) nk_widget_disable_end(context);
    }
    render_storyboard_generator_ownership(context, view, output, input_available);
}
auto lua_action(const storyboard_view_t& view, const storyboard_lua_link_view_t& row,
    storyboard_lua_action_kind_t kind) -> storyboard_action_t {
    auto action = storyboard_action_t::from(view, storyboard_action_kind_t::lua_link, view.state.selection.storyboard);
    action.lua_link = {.kind = kind, .target = row.target, .expected_link_id = row.link_id,
        .form = row.form, .selected_keys = view.state.selection.keys};
    return action;
}
void lua_status(nk_context* context, const storyboard_lua_link_view_t& row) {
    nk_layout_row_dynamic(context, 40, 1);
    const auto title = "ƒ " + row.form.module + ":" + (row.form.entry_point.empty() ? "default" : row.form.entry_point);
    nk_label_wrap(context, title.c_str());
    nk_layout_row_dynamic(context, 22, 1);
    const auto status = row.pending ? "Waiting for Lua worker…" : row.last_good ? "Last good result retained" : "Frozen source " + row.source_hash.substr(0, 12);
    nk_label(context, status.c_str(), NK_TEXT_LEFT);
    if (!row.error.empty()) { nk_layout_row_dynamic(context, 56, 1); nk_label_wrap(context, row.error.c_str()); }
}
void lua_controls(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output,
    const storyboard_lua_target_t& target) {
    const auto* row = view.inspector.lua_link_for(target);
    if (!row) return;
    if (!row->supported) { nk_layout_row_dynamic(context, 44, 1); nk_label_wrap(context, row->reason.c_str()); return; }
    if (!row->link_id.empty()) lua_status(context, *row);
    if (!row->editable) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 1);
    const auto* title = row->link_id.empty() ? target.children ? "Fill from Lua…" : "Link to Lua…" : "Edit Lua link…";
    if (nk_button_label(context, title) && row->editable) { input.reset(); lua_input.open(view, *row); }
    if (!row->link_id.empty()) {
        nk_layout_row_dynamic(context, 25, 1);
        if (nk_button_label(context, "Reload")) emit(output, lua_action(view, *row, storyboard_lua_action_kind_t::reload));
        nk_layout_row_dynamic(context, 25, 1);
        if (nk_button_label(context, target.children ? "Detach children" : "Bake to keys"))
            emit(output, lua_action(view, *row, target.children ? storyboard_lua_action_kind_t::detach_children : storyboard_lua_action_kind_t::bake));
        nk_layout_row_dynamic(context, 25, 1);
        if (nk_button_label(context, "Unlink")) emit(output, lua_action(view, *row, storyboard_lua_action_kind_t::unlink));
    }
    if (!row->editable) nk_widget_disable_end(context);
}
void SDLCALL lua_module_picked(void* userdata, const char* const* paths, int) {
    const auto generation = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userdata));
    lua_input.picker.complete(generation, paths && paths[0] ? std::string{paths[0]} : std::string{},
        paths ? std::string{} : std::string{SDL_GetError()}, paths && !paths[0]);
}
void choose_lua_module() {
    const auto generation = lua_input.picker.begin();
    if (!generation) return;
    lua_input.picking = true;
    static constexpr SDL_DialogFileFilter filter{"Lua modules", "lua"};
    SDL_ShowOpenFileDialog(lua_module_picked, reinterpret_cast<void*>(static_cast<std::uintptr_t>(*generation)),
        SDL_GetKeyboardFocus(), &filter, 1, nullptr, false);
}
void consume_lua_module() {
    const auto picked = lua_input.picker.take();
    if (!picked || !lua_input.captured) return;
    lua_input.picking = false;
    if (!*picked) { lua_input.error = picked->error().message; return; }
    if (picked->value().path.size() >= lua_input.module.size()) {
        lua_input.error = "Lua module path exceeds the supported path length."; return;
    }
    if (!picked->value().canceled) lua_input_t::text(lua_input.module, picked->value().path);
}
template<std::size_t Size>
void lua_text_field(nk_context* context, const char* title, std::array<char, Size>& value) {
    nk_layout_row_dynamic(context, 25, 2); nk_label(context, title, NK_TEXT_LEFT);
    (void)chrome::worm_ui_nuklear_text_field(context,
        static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER, value.data(), static_cast<int>(value.size()), nk_filter_default);
}
void lua_parameter_fields(nk_context* context) {
    nk_layout_row_dynamic(context, 110, 1);
    if (nk_group_begin(context, "Lua named parameters", NK_WINDOW_BORDER)) {
        for (auto& parameter : lua_input.parameters) {
            lua_text_field(context, "Name", parameter.name); lua_text_field(context, "Value", parameter.value);
        }
        nk_group_end(context);
    }
    nk_layout_row_dynamic(context, 25, 2);
    if (nk_button_label(context, "Add parameter")) {
        auto& draft = lua_input.parameters.emplace_back();
        lua_input_t::text(draft.name, "parameter" + std::to_string(lua_input.parameters.size()));
        lua_input_t::text(draft.value, "0");
    }
    if (nk_button_label(context, "Remove last parameter") && !lua_input.parameters.empty()) lua_input.parameters.pop_back();
}
void lua_form_fields(nk_context* context) {
    lua_text_field(context, "Module file", lua_input.module);
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, "Choose module…")) choose_lua_module();
    lua_text_field(context, "Entry point", lua_input.entry);
    lua_text_field(context, "Seed", lua_input.seed);
    if (!lua_input.captured->lua_link.target.children) {
        lua_text_field(context, "Begin (ms)", lua_input.begin); lua_text_field(context, "End (ms)", lua_input.end);
    }
    lua_parameter_fields(context);
}
void defer_new_field_capture(const storyboard_action_t& output) {
    const bool terminal = output.inspector.phase == phase_t::commit || output.inspector.phase == phase_t::apply ||
                          output.inspector.phase == phase_t::cancel;
    if (output.kind != storyboard_action_kind_t::inspector || !terminal) return;
    for (auto& [key, draft] : input.drafts) {
        (void)key;
        if (draft.active) draft.deferred_capture = true;
    }
}
} // namespace

void cancel_storyboard_inspector_input() {
    input.reset();
    // Native picker focus loss cancels gestures but its own captured result stays
    // valid until the slot completes or a context/stamp/selection change resets it.
    if (!lua_input.picking) lua_input.reset();
}
auto drain_storyboard_inspector_cancel(storyboard_action_t& output) -> bool {
    if (!input.pending_cancel || !emit(output, *input.pending_cancel)) return false;
    input.pending_cancel.reset();
    return true;
}
bool capture_storyboard_inspector_key(nk_context* context, const SDL_KeyboardEvent& event) {
    if (input.escape_captured && event.key == SDLK_ESCAPE) {
        if (!event.down) input.escape_captured = false;
        return true;
    }
    if (lua_input.captured) {
        if (event.key == SDLK_ESCAPE) {
            if (event.down) { lua_input.reset(); input.escape_captured = true; }
        } else chrome::forward_modal_key(context, event);
        return true;
    }
    if (!input.owns_keyboard()) return false;
    if (event.key == SDLK_ESCAPE) {
        if (event.down && !event.repeat) { input.escape = true; input.escape_captured = true; }
    } else chrome::forward_modal_key(context, event);
    return true;
}
void render_storyboard_inspector(nk_context* context, const storyboard_view_t& view,
                                storyboard_action_t& output, bool input_available) {
    input.bind(view);
    if (drain_storyboard_inspector_cancel(output)) return;
    nk_layout_row_dynamic(context, 24, 1); nk_label(context, "Inspector", NK_TEXT_LEFT);
    const auto& inspector = view.inspector;
    readonly_actions(context, view, output, input_available);
    if (inspector.node.empty()) { input.escape = false; return; }
    const bool editable = input_available && inspector.editable;
    if (!editable) input.reset();
    if (drain_storyboard_inspector_cancel(output)) return;
    input.available = editable;
    if (!editable) nk_widget_disable_begin(context);
    text_property(context, view, output, "ID", field_t::id, inspector.node);
    if (inspector.is_sprite) image_property(context, view, output);
    origin_property(context, view, output);
    layer_property(context, view, output);
    order_property(context, view, output);
    lifetime_properties(context, view, output);
    blend_property(context, view, output);
    transform_stack(context, view, output);
    numeric_track(context, view, output, inspector.opacity.keyed ? "Opacity ◆" : "Opacity ◇", field_t::opacity, inspector.opacity);
    numeric_track(context, view, output, inspector.color.keyed ? "Color ◆" : "Color ◇", field_t::color, inspector.color);
    lua_controls(context, view, output, {.track = {.node = inspector.node, .kind = osbx_track_kind_t::blend}});
    lua_controls(context, view, output, {.track = {.node = inspector.node, .kind = osbx_track_kind_t::visibility}});
    lua_controls(context, view, output, {.track = {.node = inspector.node}, .children = true});
    if (!editable) nk_widget_disable_end(context);
    matrix_readout(context, inspector);
    // A blur commit or terminal scrub wins over a simultaneous focus intent.
    // The newly focused field captures the acknowledged next-frame document,
    // while unrelated refreshes during an existing draft remain stale.
    defer_new_field_capture(output);
    input.escape = false;
}
void reset_storyboard_lua_input() { lua_input.reset(); }
void prepare_storyboard_lua_input(const storyboard_view_t& view) { lua_input.bind(view); }
auto storyboard_lua_editor_active() -> bool { return lua_input.captured.has_value(); }
void open_storyboard_lua_editor(const storyboard_view_t& view, const storyboard_lua_target_t& target) {
    const auto* row = view.inspector.lua_link_for(target);
    if (row && row->editable) { input.reset(); lua_input.open(view, *row); }
}
auto begin_storyboard_lua_picker_for_testing() -> std::optional<std::uint64_t> {
    const auto generation = lua_input.picker.begin();
    if (generation) lua_input.picking = true;
    return generation;
}
void complete_storyboard_lua_picker_for_testing(std::uint64_t generation, std::string path) {
    lua_input.picker.complete(generation, std::move(path), {}, false);
}
void render_storyboard_lua_editor(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output) {
    lua_input.bind(view);
    if (!lua_input.captured) return;
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Link to Lua", NK_WINDOW_BORDER | NK_WINDOW_MOVABLE,
        nk_rect(12, 30, 520, 480))) {
        if (!lua_input.picking) lua_input.reset();
        return;
    }
    consume_lua_module(); lua_form_fields(context);
    if (!lua_input.error.empty()) { nk_layout_row_dynamic(context, 44, 1); nk_label_wrap(context, lua_input.error.c_str()); }
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "Apply Lua link") && !lua_input.picker.open() && lua_input.parse()) {
        if (emit(output, *lua_input.captured)) { nk_popup_close(context); lua_input.reset(); }
    }
    if (nk_button_label(context, "Cancel")) { nk_popup_close(context); lua_input.reset(); }
    nk_popup_end(context);
}
} // namespace wormedit::ui

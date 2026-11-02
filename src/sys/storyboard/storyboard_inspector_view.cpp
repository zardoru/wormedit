#include "sys/storyboard/storyboard_inspector_view.h"
#include "ui/frontend/storyboard.h"
#include <wormedit/core/snap.h>
#include <wormedit/core/osbx_lua_link.h>
#include "core/text_util.h"
#include <array>

namespace wormedit::sys {
namespace {
constexpr std::array<std::string_view, 6> transform_labels{
    "Translate", "Rotate (degrees)", "Scale", "Skew (degrees)", "Matrix", "Pivot"};
static_assert(transform_labels.size() == static_cast<std::size_t>(osbx_transform_kind_t::pivot) + 1);

auto find_storyboard(const song_document_t& song, storyboard_id_t id) -> const storyboard_document_t* {
    const auto found = std::ranges::find(song.storyboards, id, &storyboard_document_t::id);
    return found == song.storyboards.end() ? nullptr : &*found;
}
auto find_node(const osbx_document_t& scene, std::string_view id) -> const osbx_node_t* {
    const auto found = std::ranges::find(scene.nodes, id, &osbx_node_t::id);
    return found == scene.nodes.end() ? nullptr : &*found;
}
struct inspector_op_source_t {
    const osbx_transform_op_t* op{};
    std::optional<std::string> parent;
    std::size_t depth{}, index{}, count{};
};
void collect_ops(std::span<const osbx_transform_op_t> stack, const std::optional<std::string>& parent,
                 std::size_t depth, std::vector<inspector_op_source_t>& result) {
    for (std::size_t i = 0; i < stack.size(); ++i) {
        const auto& op = stack[i];
        result.push_back({.op = &op, .parent = parent, .depth = depth, .index = i, .count = stack.size()});
        collect_ops(op.children, op.id, depth + 1, result);
    }
}
template<class Value>
auto has_key_at(const osbx_track_t<Value>& track, storyboard_time_t time) -> bool {
    return time.is_valid() && time.remainder == 0 &&
        std::ranges::find(track.keys, time.milliseconds, &osbx_key_t<Value>::time_ms) != track.keys.end();
}
template<class Value>
auto occupied_key(const osbx_track_t<Value>& track, storyboard_time_t time) -> std::optional<std::string> {
    const auto snapped = snap_storyboard_key_time(time);
    if (!snapped) return {};
    const auto found = std::ranges::find(track.keys, *snapped, &osbx_key_t<Value>::time_ms);
    return found == track.keys.end() ? std::nullopt : std::optional{found->id};
}
void build_numeric_track_view(ui::storyboard_numeric_track_view_t& view, const osbx_track_t<>& track,
                              storyboard_time_t time, bool editable) {
    view.value = sample_osbx_numeric_track(track, time);
    view.keyed = !track.keys.empty();
    view.at_key = has_key_at(track, time);
    view.key = occupied_key(track, time);
    const auto key_time = snap_storyboard_key_time(time);
    const bool direct = key_time && track.can_edit_key_at(*key_time);
    view.editable = editable && direct && !view.value.empty();
    view.reason = direct ? "" : "Linked to Lua: bake or unlink to edit.";
}
auto build_spline_view(const osbx_transform_op_t& op, std::optional<std::int64_t> time, bool editable,
    std::span<const double> value)
    -> std::optional<storyboard_spline_t> {
    const bool rotation = op.kind == osbx_transform_kind_t::rotate;
    if ((!rotation && op.kind != osbx_transform_kind_t::translate && op.kind != osbx_transform_kind_t::scale) ||
        !editable || !time || *time > INT64_MAX - 1000 || op.track.lua || !op.track.program.empty()) return {};
    storyboard_spline_t curve{.begin_ms = *time, .end_ms = *time + 1000, .dimensions = rotation ? 1u : 2u,
        .tolerance = rotation ? 0.1 : op.kind == osbx_transform_kind_t::scale ? 0.001 : 0.5};
    if (value.size() != curve.dimensions) return {};
    for (auto& point : curve.points) std::copy(value.begin(), value.end(), point.begin());
    return curve;
}
void build_node_view(ui::storyboard_inspector_view_t& view, const storyboard_document_t& storyboard,
                     const osbx_node_t& node) {
    view.node = node.id;
    view.reason = storyboard_node_edit_reason(storyboard, node.id);
    if (view.reason.empty() && !view.key_time_ms)
        view.reason = "The playhead cannot be represented as a storyboard key time.";
    view.editable = view.reason.empty() && view.key_time_ms.has_value();
    view.is_sprite = node.kind == osbx_node_kind_t::sprite;
    view.image = node.image;
    view.origin = node.origin;
    view.layer = node.layer;
    view.order = node.order;
    view.lifetime = node.lifetime;
    build_numeric_track_view(view.opacity, node.tracks.opacity, view.time, view.editable);
    build_numeric_track_view(view.color, node.tracks.color, view.time, view.editable);
    const auto blend = sample_osbx_blend_track(node.tracks.blend, view.time);
    view.blend = blend.value_or(node.tracks.blend.default_value);
    view.blend_editable = view.editable && !node.tracks.blend.lua && blend.has_value();
    view.blend_keyed = !node.tracks.blend.keys.empty();
    view.blend_at_key = has_key_at(node.tracks.blend, view.time);
    view.blend_key = occupied_key(node.tracks.blend, view.time);
    std::vector<inspector_op_source_t> sources;
    collect_ops(node.ops, {}, 0, sources);
    for (const auto& source : sources) {
        const auto& op = *source.op;
        auto& row = view.ops.emplace_back(ui::storyboard_op_view_t{
            .id = op.id, .parent = source.parent, .kind = op.kind,
            .label = std::string(storyboard_transform_label(op.kind)),
            .depth = source.depth, .ordinal = source.index + 1,
            .can_move_up = source.index != 0, .can_move_down = source.index + 1 < source.count});
        build_numeric_track_view(row.track, op.track, view.time, view.editable);
        row.spline = build_spline_view(op, view.key_time_ms, view.editable, row.track.value);
    }
}
void sample_prepared_values(ui::storyboard_inspector_view_t& view, const osbx_node_t& sampled) {
    view.opacity.value = sample_osbx_numeric_track(sampled.tracks.opacity, view.time);
    view.color.value = sample_osbx_numeric_track(sampled.tracks.color, view.time);
    view.blend = sample_osbx_blend_track(sampled.tracks.blend, view.time).value_or(view.blend);
    std::vector<inspector_op_source_t> sources;
    collect_ops(sampled.ops, {}, 0, sources);
    for (auto& row : view.ops) {
        const auto found = std::ranges::find_if(sources, [&](const auto& source) { return source.op->id == row.id; });
        if (found != sources.end()) row.track.value = sample_osbx_numeric_track(found->op->track, view.time);
    }
}
auto plan_numeric_edit(const osbx_node_t& node, const ui::storyboard_inspector_action_t& action)
    -> result_t<osbx_edit_change_t, failure_code_t> {
    osbx_track_target_t target{.node = node.id};
    switch (action.field) {
    case ui::storyboard_inspector_field_t::opacity:
        return plan_storyboard_track_edit(node.tracks.opacity, target, action.value, action.key_time_ms, action.key);
    case ui::storyboard_inspector_field_t::color:
        target.kind = osbx_track_kind_t::color;
        return plan_storyboard_track_edit(node.tracks.color, target, action.value, action.key_time_ms, action.key);
    case ui::storyboard_inspector_field_t::blend:
        target.kind = osbx_track_kind_t::blend;
        return plan_storyboard_track_edit(node.tracks.blend, target, action.blend, action.key_time_ms, action.key);
    case ui::storyboard_inspector_field_t::transform: {
        std::vector<inspector_op_source_t> ops;
        collect_ops(node.ops, {}, 0, ops);
        const auto found = std::ranges::find_if(ops, [&](const auto& item) { return item.op->id == action.op; });
        if (found == ops.end()) return std::unexpected(failure_code_t::not_found);
        target.kind = osbx_track_kind_t::transform;
        target.op = action.op;
        return plan_storyboard_track_edit(found->op->track, target, action.value, action.key_time_ms, action.key);
    }
    default: return std::unexpected(failure_code_t::invalid_request);
    }
}
auto plan_op_move(const osbx_node_t& node, const ui::storyboard_inspector_action_t& action)
    -> result_t<osbx_edit_change_t, failure_code_t> {
    std::vector<inspector_op_source_t> ops;
    collect_ops(node.ops, {}, 0, ops);
    const auto found = std::ranges::find_if(ops, [&](const auto& item) { return item.op->id == action.op; });
    if (found == ops.end()) return std::unexpected(failure_code_t::not_found);
    if ((action.move != -1 && action.move != 1) ||
        (action.move == -1 && found->index == 0) ||
        (action.move == 1 && found->index + 1 == found->count))
        return std::unexpected(failure_code_t::invalid_request);
    const auto destination = action.move == -1 ? found->index - 1 : found->index + 1;
    return osbx_reorder_op_t{.node = node.id, .op = action.op, .index = destination};
}
auto lua_track(const osbx_node_t& node, const osbx_track_target_t& target) -> const osbx_track_t<>* {
    if (target.kind == osbx_track_kind_t::opacity) return &node.tracks.opacity;
    if (target.kind == osbx_track_kind_t::color) return &node.tracks.color;
    if (target.kind != osbx_track_kind_t::transform) return nullptr;
    std::vector<inspector_op_source_t> ops;
    collect_ops(node.ops, {}, 0, ops);
    const auto found = std::ranges::find_if(ops, [&](const auto& item) { return item.op->id == target.op; });
    return found == ops.end() || found->op->kind == osbx_transform_kind_t::pivot ? nullptr : &found->op->track;
}
struct lua_target_binding_t {
    const osbx_track_t<>* track{};
    const osbx_lua_link_t* link{};
};
auto lua_binding(const song_document_t& song, storyboard_id_t id, const ui::storyboard_lua_action_t& action)
    -> result_t<lua_target_binding_t, std::string> {
    const auto* board = find_storyboard(song, id);
    if (!board || !board->osbx) return std::unexpected("Choose an editable OSBX storyboard.");
    const auto& target = action.target;
    using kind_t = ui::storyboard_lua_action_kind_t;
    if ((action.kind == kind_t::fill || action.kind == kind_t::detach_children) && !target.children)
        return std::unexpected("This Lua action requires a group-children target.");
    if ((action.kind == kind_t::link || action.kind == kind_t::bake) && target.children)
        return std::unexpected("This Lua action requires a numeric-track target.");
    const auto reason = storyboard_node_edit_reason(*board, target.track.node);
    if (!reason.empty()) return std::unexpected(reason);
    const auto* node = find_node(*board->osbx, target.track.node);
    lua_target_binding_t result;
    if (target.children) {
        if (node->kind != osbx_node_kind_t::group) return std::unexpected("Only groups support scripted children.");
        result.link = node->children_link ? &*node->children_link : nullptr;
    } else {
        result.track = lua_track(*node, target.track);
        if (!result.track) return std::unexpected("Only numeric non-pivot tracks support Lua links.");
        result.link = result.track->lua ? &result.track->lua->link : nullptr;
    }
    if (action.expected_link_id != (result.link ? result.link->id : std::string{}))
        return std::unexpected("Lua link identity changed.");
    return result;
}
struct lua_span_t {
    std::span<const std::string> selection;
    std::optional<std::pair<std::int64_t, std::int64_t>> whole, selected;
    static void include(std::optional<std::pair<std::int64_t, std::int64_t>>& span, std::int64_t time) {
        if (!span) span = std::pair{time, time};
        span->first = std::min(span->first, time); span->second = std::max(span->second, time);
    }
    template<class Value> void track(const osbx_track_t<Value>& source) {
        for (const auto& key : source.keys) {
            include(whole, key.time_ms);
            if (std::ranges::find(selection, key.id) != selection.end()) include(selected, key.time_ms);
        }
        if (source.lua) { include(whole, source.lua->begin_ms); include(whole, source.lua->end_ms); }
    }
    void node(const osbx_node_t& source) {
        if (source.lifetime) { include(whole, source.lifetime->begin_ms); include(whole, source.lifetime->end_ms); }
        track(source.tracks.opacity); track(source.tracks.color); track(source.tracks.visibility); track(source.tracks.blend);
        std::vector<inspector_op_source_t> ops;
        collect_ops(source.ops, {}, 0, ops);
        for (const auto& op : ops) track(op.op->track);
    }
    auto range() const -> std::pair<std::int64_t, std::int64_t> {
        if (selected && selected->first < selected->second) return *selected;
        if (whole && whole->first < whole->second) return *whole;
        return {0, osbx_lua_track_link_t{}.end_ms};
    }
};
void refill_lua_metadata(ui::storyboard_lua_link_view_t& row, const osbx_lua_link_t* link,
    const osbx_lua_prepared_scene_t* prepared) {
    row.link_id.clear(); row.source_hash.clear(); row.error.clear(); row.sample_times_ms.clear();
    row.last_good = false; row.pending = false;
    if (!link) return;
    row.link_id = link->id;
    row.form.module = link->source.source_reference ? text_util::path_utf8(*link->source.source_reference) : "";
    row.form.entry_point = link->source.entry_point; row.form.parameters = link->source.parameters;
    row.form.seed = link->source.seed; row.source_hash = link->source.source_hash;
    row.pending = !prepared;
    if (!prepared) return;
    const auto diagnostic = std::ranges::find(prepared->diagnostics, link->id,
        [](const auto& value) -> const std::string& { return value.location.link_id; });
    if (diagnostic != prepared->diagnostics.end()) row.error = diagnostic->message;
    const auto result = std::ranges::find_if(prepared->links, [&](const auto& value) { return value.link.id == link->id; });
    if (result == prepared->links.end()) return;
    row.last_good = result->is_last_good;
    for (const auto& sample : result->samples) row.sample_times_ms.push_back(sample.time_ms);
}
struct lua_views_t {
    ui::storyboard_inspector_view_t& view;
    const storyboard_document_t& board;
    const osbx_lua_prepared_scene_t* prepared;
    std::pair<std::int64_t, std::int64_t> range;
    std::size_t used{};
    void append(const osbx_node_t& node, ui::storyboard_lua_target_t target, const osbx_track_t<>* track) {
        if (used == view.lua_links.size()) view.lua_links.emplace_back();
        auto& row = view.lua_links[used++];
        row.target = std::move(target); row.form.module.clear(); row.form.entry_point.clear(); row.form.parameters.clear();
        row.form.seed = 0; row.form.begin_ms = range.first; row.form.end_ms = range.second;
        row.reason = storyboard_node_edit_reason(board, node.id);
        row.supported = row.target.children || track;
        if (!row.supported) row.reason = "Lua links unavailable: numeric tracks only.";
        row.editable = row.supported && row.reason.empty();
        const auto* link = row.target.children ? (node.children_link ? &*node.children_link : nullptr)
            : track && track->lua ? &track->lua->link : nullptr;
        if (track && track->lua) { row.form.begin_ms = track->lua->begin_ms; row.form.end_ms = track->lua->end_ms; }
        refill_lua_metadata(row, link, prepared);
    }
    void node(const osbx_node_t& source) {
        std::vector<inspector_op_source_t> ops;
        collect_ops(source.ops, {}, 0, ops);
        for (const auto& op : ops)
            append(source, {.track = {.node = source.id, .kind = osbx_track_kind_t::transform, .op = op.op->id}},
                op.op->kind == osbx_transform_kind_t::pivot ? nullptr : &op.op->track);
        append(source, {.track = {.node = source.id, .kind = osbx_track_kind_t::opacity}}, &source.tracks.opacity);
        append(source, {.track = {.node = source.id, .kind = osbx_track_kind_t::color}}, &source.tracks.color);
        append(source, {.track = {.node = source.id, .kind = osbx_track_kind_t::visibility}}, nullptr);
        append(source, {.track = {.node = source.id, .kind = osbx_track_kind_t::blend}}, nullptr);
        if (source.kind == osbx_node_kind_t::group) append(source, {.track = {.node = source.id}, .children = true}, nullptr);
    }
};
void build_lua_links(ui::storyboard_view_t& root, const song_document_t& song, const storyboard_document_t& board) {
    lua_span_t span{.selection = root.state.selection.keys};
    for (const auto& node : board.osbx->nodes) span.node(node);
    lua_views_t builder{.view = root.inspector, .board = board,
        .prepared = osbx_lua_prepared_scope_t::prepared(*board.osbx, document_stamp_t::from(song), board.id,
            root.canvas.prepared_generation), .range = span.range()};
    for (const auto& node : board.osbx->nodes)
        if (std::ranges::find(root.state.selection.nodes, node.id) != root.state.selection.nodes.end()) builder.node(node);
    root.inspector.lua_links.resize(builder.used);
}
} // namespace

auto storyboard_node_edit_reason(const storyboard_document_t& storyboard, std::string_view id) -> std::string {
    if (storyboard.owning_generator_recipe) return "Generated storyboard: detach before editing.";
    if (storyboard.model || !storyboard.osbx) return "Imported OSB storyboard: convert to OSBX before editing.";
    const auto* ancestor = find_node(*storyboard.osbx, id);
    if (!ancestor) return "Node is not authored: detach before editing.";
    // Documents are validated; the bound also keeps malformed previews finite.
    for (std::size_t i = 0; ancestor->parent && i < storyboard.osbx->nodes.size(); ++i) {
        ancestor = find_node(*storyboard.osbx, *ancestor->parent);
        if (!ancestor) break;
        if (ancestor->children_link) return "Lua-linked children: detach before editing.";
    }
    return {};
}

auto storyboard_transform_label(osbx_transform_kind_t kind) -> std::string_view {
    const auto index = static_cast<std::size_t>(kind);
    return index < transform_labels.size() ? transform_labels[index] : std::string_view{};
}

void build_storyboard_inspector_view(ui::storyboard_view_t& root, const song_document_t& song,
    storyboard_time_t time, const osbx_node_frame_t* evaluated, const osbx_document_t* prepared_scene) {
    auto& view = root.inspector;
    view.node.clear(); view.reason.clear(); view.ops.clear(); view.images.clear(); view.origins.clear();
    view.transform_choices.clear(); view.world.reset();
    view.editable = false; view.can_convert = false;
    view.time = time;
    const auto snapped = snap_storyboard_key_time(time);
    view.key_time_ms = snapped ? std::optional{*snapped} : std::nullopt;
    const auto* storyboard = find_storyboard(song, root.state.selection.storyboard);
    if (!storyboard) { view.lua_links.clear(); view.message = "Choose a storyboard."; return; }
    view.can_convert = storyboard->model.has_value() && !storyboard->owning_generator_recipe;
    if (!storyboard->osbx) {
        view.lua_links.clear();
        view.message = "Imported OSB storyboard: convert to OSBX before editing.";
        view.reason = view.message;
        return;
    }
    build_lua_links(root, song, *storyboard);
    for (const auto& image : storyboard->osbx->images)
        view.images.push_back({.id = image.id, .label = image.file});
    if (root.state.selection.nodes.size() != 1) {
        view.message = "Select one node to inspect.";
        return;
    }
    const auto& selected = root.state.selection.nodes[0];
    const auto* authored = find_node(*storyboard->osbx, selected);
    const auto* sampled = prepared_scene ? find_node(*prepared_scene, selected) : authored;
    const auto* node = authored ? authored : sampled;
    if (!node) { view.message = "Selected node no longer exists."; return; }
    view.message.clear();
    build_node_view(view, *storyboard, *node);
    if (sampled) sample_prepared_values(view, *sampled);
    for (const auto& origin : storyboard_origin_t::catalog())
        view.origins.push_back({.id = std::string(origin.name), .label = std::string(origin.name)});
    for (std::size_t i = 0; i < transform_labels.size(); ++i)
        view.transform_choices.push_back({.kind = static_cast<osbx_transform_kind_t>(i),
                                         .label = std::string(storyboard_transform_label(static_cast<osbx_transform_kind_t>(i)))});
    if (evaluated && evaluated->is_finite()) view.world = evaluated->matrix;
}

auto storyboard_lua_source(const song_document_t& song, storyboard_id_t id,
    const ui::storyboard_lua_action_t& action) -> result_t<lua_source_descriptor_t, std::string> {
    const auto binding = lua_binding(song, id, action);
    if (!binding) return std::unexpected(binding.error());
    if (action.kind == ui::storyboard_lua_action_kind_t::reload) {
        if (!binding->link) return std::unexpected("Lua link no longer exists.");
        return binding->link->source;
    }
    lua_source_descriptor_t source;
    source.source_reference = text_util::path_from_utf8(action.form.module);
    source.entry_point = action.form.entry_point; source.parameters = action.form.parameters; source.seed = action.form.seed;
    return source;
}
auto plan_storyboard_lua_edit(const song_document_t& song, storyboard_id_t id,
    const ui::storyboard_lua_action_t& action, const lua_source_descriptor_t* frozen)
    -> result_t<osbx_edit_change_t, std::string> {
    const auto binding = lua_binding(song, id, action);
    if (!binding) return std::unexpected(binding.error());
    const bool unlink = action.kind == ui::storyboard_lua_action_kind_t::unlink;
    if (!unlink && !frozen) return std::unexpected("Lua module has not been frozen by its worker.");
    if (action.target.children) {
        std::optional<osbx_lua_link_t> link;
        if (!unlink) link = osbx_lua_link_t{.source = *frozen};
        return osbx_set_lua_children_t{.node = action.target.track.node,
            .expected_link = action.expected_link_id, .link = std::move(link)};
    }
    std::optional<osbx_lua_track_link_t> link;
    if (!unlink) {
        link = binding->track->lua.value_or(osbx_lua_track_link_t{});
        link->link.source = *frozen;
        if (action.kind != ui::storyboard_lua_action_kind_t::reload) {
            link->begin_ms = action.form.begin_ms; link->end_ms = action.form.end_ms;
        }
    }
    return osbx_set_lua_track_t{.track = action.target.track,
        .expected_link = action.expected_link_id, .link = std::move(link)};
}

auto plan_storyboard_inspector_edit(const song_document_t& song, storyboard_id_t id,
    const ui::storyboard_inspector_action_t& action) -> result_t<osbx_edit_change_t, failure_code_t> {
    if (!action.has_valid_value) return std::unexpected(failure_code_t::invalid_data);
    const auto* storyboard = find_storyboard(song, id);
    if (storyboard && action.field == ui::storyboard_inspector_field_t::convert)
        return osbx_convert_storyboard_t{};
    if (!storyboard || !storyboard->osbx) return std::unexpected(failure_code_t::not_found);
    const auto* node = find_node(*storyboard->osbx, action.node);
    if (!node) return std::unexpected(failure_code_t::not_found);
    if (!storyboard_node_edit_reason(*storyboard, node->id).empty()) return std::unexpected(failure_code_t::unavailable);
    using field = ui::storyboard_inspector_field_t;
    switch (action.field) {
    case field::id: return osbx_rename_node_t{.node = node->id, .new_id = action.text};
    case field::image: return osbx_set_image_t{.node = node->id,
        .image = action.text.empty() ? std::nullopt : std::optional{action.text}};
    case field::origin: return osbx_set_origin_t{.node = node->id, .origin = action.text};
    case field::layer: return osbx_set_layer_t{.node = node->id, .layer = action.layer};
    case field::order: return osbx_set_order_t{.node = node->id, .order = action.order};
    case field::lifetime: return osbx_set_lifetime_t{.node = node->id, .lifetime = action.lifetime};
    case field::add_op: return osbx_add_op_t{.node = node->id, .parent_op = action.parent_op, .kind = action.op_kind};
    case field::remove_op: return osbx_remove_op_t{.node = node->id, .op = action.op};
    case field::move_op: return plan_op_move(*node, action);
    case field::bake_spline:
        return osbx_bake_spline_t{.track = {.node = node->id, .kind = osbx_track_kind_t::transform, .op = action.op},
            .spline = action.spline};
    case field::transform: case field::opacity: case field::color: case field::blend:
        return plan_numeric_edit(*node, action);
    case field::none: case field::convert: return std::unexpected(failure_code_t::invalid_request);
    }
    return std::unexpected(failure_code_t::invalid_request);
}
} // namespace wormedit::sys

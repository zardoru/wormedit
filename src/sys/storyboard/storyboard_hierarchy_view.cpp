#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include <wormedit/core/storyboard_canvas.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit::sys {
namespace {
void append_warning(std::string& warning, std::string_view reason) {
    if (reason.empty()) return;
    if (!warning.empty()) warning += "; ";
    warning += reason;
}
struct hierarchy_position_t {
    const osbx_node_t* node{};
    std::size_t depth{};
};
// A hierarchy shows sibling draw order with parents immediately above their
// descendants. Stable sorting retains document order when authored order ties.
auto hierarchy_order(const osbx_document_t& scene, osbx_layer_t layer)
    -> std::vector<hierarchy_position_t> {
    std::vector<const osbx_node_t*> ordered;
    for (const auto& node : scene.nodes)
        if (node.layer == layer) ordered.push_back(&node);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return a->order < b->order;
    });
    std::unordered_map<std::string_view, std::vector<const osbx_node_t*>> children;
    std::vector<hierarchy_position_t> pending;
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        if ((*it)->parent) continue;
        pending.push_back({.node = *it});
    }
    for (const auto* node : ordered)
        if (node->parent) children[*node->parent].push_back(node);
    std::vector<hierarchy_position_t> result;
    std::unordered_set<std::string_view> visited;
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        if (!visited.insert(item.node->id).second) continue;
        result.push_back(item);
        const auto found = children.find(item.node->id);
        if (found == children.end()) continue;
        for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
            pending.push_back({.node = *it, .depth = item.depth + 1});
    }
    return result;
}
auto hierarchy_warnings(const osbx_document_t& scene, std::span<const osbx_osb_report_t> reports)
    -> std::unordered_map<std::string, std::string> {
    const auto known = reports.empty() ? osbx_osb_known_unsupported(scene) : std::vector<osbx_osb_report_t>{};
    const auto effective = reports.empty() ? std::span<const osbx_osb_report_t>{known} : reports;
    std::unordered_map<std::string, std::string> result;
    for (const auto& report : effective)
        if (report.classification == osbx_osb_class_t::unsupported) result[report.node_id] = report.reason;
    const auto validation = validate_osbx_document(scene);
    for (const auto& diagnostic : validation.diagnostics) {
        if (diagnostic.code != osbx_diagnostic_code_t::missing_image) continue;
        auto& reason = result[diagnostic.location.id];
        append_warning(reason, diagnostic.message);
    }
    return result;
}
void refill_hierarchy_selection(ui::storyboard_view_t& view) {
    const std::unordered_set<std::string_view> selected{view.state.selection.nodes.begin(),
                                                       view.state.selection.nodes.end()};
    const std::unordered_set<std::string_view> hidden{view.state.hidden_nodes.begin(), view.state.hidden_nodes.end()};
    for (auto& layer : view.hierarchy.layers) {
        layer.shown_in_state = is_storyboard_layer_visible(layer.title, view.state.conditional_layer);
        for (auto& row : layer.rows) {
            row.selected = selected.contains(row.id);
            row.editor_hidden = hidden.contains(row.id);
        }
    }
}
void build_imported_hierarchy(ui::storyboard_view_t& view, const storyboard_document_model_t& model) {
    for (auto& layer : view.hierarchy.layers) {
        std::vector<const storyboard_sprite_t*> sprites;
        for (const auto& sprite : model.sprites)
            if (sprite.layer == layer.title) sprites.push_back(&sprite);
        std::stable_sort(sprites.begin(), sprites.end(), [](const auto* a, const auto* b) {
            return a->source_order < b->source_order;
        });
        for (const auto* sprite : sprites) layer.rows.push_back({
            .id = storyboard_canvas_target_t::legacy_node_id(sprite->stable_id), .layer = layer.layer});
    }
}
void build_hierarchy(ui::storyboard_view_t& view, const song_document_t& document,
                     const osbx_document_t* prepared, std::span<const osbx_osb_report_t> reports) {
    for (std::size_t i = 0; i < osbx_layer_names.size(); ++i) {
        auto& layer = view.hierarchy.layers[i];
        layer.title = osbx_layer_names[i];
        layer.layer = static_cast<osbx_layer_t>(i);
        layer.rows.clear();
    }
    const auto found = std::ranges::find(document.storyboards, view.state.selection.storyboard,
                                         &storyboard_document_t::id);
    if (found == document.storyboards.end()) { refill_hierarchy_selection(view); return; }
    if (!found->osbx) {
        if (found->model) build_imported_hierarchy(view, *found->model);
        refill_hierarchy_selection(view);
        return;
    }
    const auto& scene = prepared ? *prepared : *found->osbx;
    const auto warnings = hierarchy_warnings(scene, reports);
    for (auto& layer : view.hierarchy.layers) {
        for (const auto& position : hierarchy_order(scene, layer.layer)) {
            const auto& node = *position.node;
            const auto reason = warnings.find(node.id);
            const auto edit_reason = storyboard_node_edit_reason(*found, node.id);
            auto warning = reason == warnings.end() ? std::string{} : reason->second;
            append_warning(warning, edit_reason);
            layer.rows.push_back({.id = node.id, .parent = node.parent, .depth = position.depth,
                .layer = layer.layer, .is_group = node.kind == osbx_node_kind_t::group,
                .editable = edit_reason.empty(),
                .lua_generated = !edit_reason.empty() && !found->owning_generator_recipe && !found->model,
                .warning = std::move(warning)});
        }
    }
    refill_hierarchy_selection(view);
}
} // namespace

void build_storyboard_hierarchy_view(ui::storyboard_view_t& view, const song_document_t& document,
                                    std::span<const osbx_osb_report_t> reports) {
    build_hierarchy(view, document, nullptr, reports);
}
void storyboard_hierarchy_projection_t::refill(ui::storyboard_view_t& view,
    const song_document_t& document, const storyboard_scene_presenter_t* scenes) {
    const auto selected = view.state.selection.storyboard;
    const auto* prepared = scenes ? scenes->scene(selected) : nullptr;
    (void)topology_.get_or_build(std::tuple{&document, document_stamp_t::from(document),
        selected, view.session_generation, view.canvas.prepared_generation, prepared}, [&] {
            build_hierarchy(view, document, prepared, {});
            warnings_.clear();
            for (const auto& layer : view.hierarchy.layers)
                for (const auto& row : layer.rows) warnings_.emplace(row.id, row.warning);
            return true;
        });
    refill_hierarchy_selection(view);
    for (auto& layer : view.hierarchy.layers) {
        for (auto& row : layer.rows) {
            row.warning = warnings_.at(row.id);
            const auto image_reason = scenes ? scenes->node_diagnostic(selected, row.id) : std::string_view{};
            append_warning(row.warning, image_reason);
        }
    }
}
} // namespace wormedit::sys

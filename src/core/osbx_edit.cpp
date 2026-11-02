#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/generator.h>
#include <wormedit/core/osb_to_osbx.h>
#include <wormedit/core/numeric_text.h>
#include "checked_math.h"
#include "interaction_gesture_runner.h"
#include "id_ceiling.h"
#include "resource_assignment_internal.h"
#include <cmath>
#include <type_traits>
#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {
constexpr std::array edit_labels{
    "Create OSBX storyboard", "Delete OSBX storyboard", "Rename OSBX storyboard",
    "Add storyboard node", "Delete storyboard subtree", "Rename storyboard node",
    "Reparent storyboard node", "Reorder storyboard node", "Set storyboard layer",
    "Set storyboard image", "Set storyboard origin", "Add storyboard transform",
    "Remove storyboard transform", "Reorder storyboard transform", "Set storyboard track default",
    "Add storyboard key", "Move storyboard key", "Set storyboard key value",
    "Set storyboard key easing", "Delete storyboard key", "Move storyboard keys",
    "Add storyboard image asset", "Remove storyboard image asset", "Set storyboard lifetime", "Convert OSB to OSBX",
    "Duplicate storyboard subtree", "Move storyboard subtree", "Set storyboard order", "Delete storyboard keys", "Place storyboard image", "Set storyboard Lua track", "Set storyboard Lua children", "Detach storyboard from generator", "Bake storyboard Bezier"};
static_assert(edit_labels.size() == std::variant_size_v<osbx_edit_change_t>);
template<class Edit> auto edit_label() -> const char* {
    return edit_labels[osbx_edit_change_t{Edit{}}.index()];
}

template<class Value> void collect_track_ids(const osbx_track_t<Value>& track,
                                            std::set<std::string, std::less<>>& ids) {
    track.visit_authored_ids([&](const auto& id) { ids.insert(id); });
    if (track.lua) ids.insert(track.lua->link.id);
}
void collect_ops(const std::vector<osbx_transform_op_t>& ops,
                 std::set<std::string, std::less<>>& ids) {
    for (const auto& op : ops) {
        ids.insert(op.id);
        collect_track_ids(op.track, ids);
        collect_ops(op.children, ids);
    }
}
void collect_uses(std::span<const osbx_clip_instance_t> uses, std::set<std::string, std::less<>>& ids) {
    for (const auto& use : uses) ids.insert(use.id);
}
void collect_clips(const osbx_document_t& scene, std::set<std::string, std::less<>>& ids) {
    for (const auto& clip : scene.clips) {
        ids.insert(clip.id); collect_ops(clip.ops, ids);
        collect_track_ids(clip.tracks.opacity, ids); collect_track_ids(clip.tracks.color, ids);
        collect_track_ids(clip.tracks.visibility, ids);
        if (clip.tracks.blend) collect_track_ids(*clip.tracks.blend, ids);
        collect_uses(clip.uses, ids);
    }
}
void collect_ids(const osbx_document_t& scene, std::set<std::string, std::less<>>& ids) {
    collect_clips(scene, ids);
    for (const auto& image : scene.images) ids.insert(image.id);
    for (const auto& node : scene.nodes) {
        ids.insert(node.id);
        collect_ops(node.ops, ids);
        collect_track_ids(node.tracks.opacity, ids);
        collect_track_ids(node.tracks.color, ids);
        collect_track_ids(node.tracks.visibility, ids);
        collect_track_ids(node.tracks.blend, ids);
        if (node.children_link) ids.insert(node.children_link->id);
        collect_uses(node.uses, ids);
    }
}
void collect_song_ids(const song_document_t& song, std::set<std::string, std::less<>>& ids) {
    for (const auto& storyboard : song.storyboards)
        if (storyboard.osbx) collect_ids(*storyboard.osbx, ids);
}
auto find_op_stack(std::vector<osbx_transform_op_t>& ops, std::string_view id)
    -> std::vector<osbx_transform_op_t>* {
    for (auto& op : ops) {
        if (op.id == id) return &ops;
        if (auto* found = find_op_stack(op.children, id)) return found;
    }
    return nullptr;
}
using track_ref_t = std::variant<osbx_track_t<>*, osbx_track_t<bool>*, osbx_track_t<osbx_blend_t>*>;

class subtree_clone_t {
  public:
    subtree_clone_t(const osbx_document_t& scene, const osbx_edit_ids_t& ids) : scene_{scene}, ids_{ids} {}
    auto reserve(std::string_view parent, std::string_view kind) -> std::string {
        auto id = ids_.next(parent, kind, scene_);
        ids_.reserved.insert(id);
        return id;
    }
    auto clone(const osbx_node_t& source, std::string id, std::optional<std::string> parent) -> osbx_node_t {
        auto copy = source;
        copy.id = std::move(id);
        copy.parent = std::move(parent);
        refresh_ops(copy.ops, copy.id);
        refresh_track(copy.tracks.opacity, copy.id);
        refresh_track(copy.tracks.color, copy.id);
        refresh_track(copy.tracks.visibility, copy.id);
        refresh_track(copy.tracks.blend, copy.id);
        if (copy.children_link) copy.children_link->id = reserve(copy.id, "link");
        for (auto& use : copy.uses) use.id = reserve(copy.id, "use");
        return copy;
    }
  private:
    const osbx_document_t& scene_;
    osbx_edit_ids_t ids_;
    template<class Value> void refresh_track(osbx_track_t<Value>& track, std::string_view parent) {
        track.visit_authored_ids([&](auto& id) { id = reserve(parent, "key"); });
        if (track.lua) track.lua->link.id = reserve(parent, "link");
    }
    void refresh_ops(std::vector<osbx_transform_op_t>& ops, std::string_view parent) {
        for (auto& op : ops) {
            op.id = reserve(parent, "op");
            refresh_track(op.track, op.id);
            refresh_ops(op.children, op.id);
        }
    }
};

// Mutation and target resolution belong to the scene edit owner. Errors are
// returned to transaction.stage, which owns rollback and publication.
class scene_edit_t {
  public:
    osbx_document_t& scene;
    const osbx_edit_ids_t& ids;
    bool changed{};

    auto node(std::string_view id) -> osbx_node_t* {
        const auto found = std::ranges::find(scene.nodes, id, &osbx_node_t::id);
        return found == scene.nodes.end() ? nullptr : &*found;
    }
    auto transform(osbx_node_t& owner, std::string_view id) -> osbx_transform_op_t* {
        auto* stack = find_op_stack(owner.ops, id);
        return stack ? &*std::ranges::find(*stack, id, &osbx_transform_op_t::id) : nullptr;
    }
    auto track(const osbx_track_target_t& target) -> std::optional<track_ref_t> {
        auto* owner = node(target.node);
        if (!owner) return {};
        switch (target.kind) {
        case osbx_track_kind_t::transform:
            if (auto* op = transform(*owner, target.op)) return &op->track;
            return {};
        case osbx_track_kind_t::opacity: return &owner->tracks.opacity;
        case osbx_track_kind_t::color: return &owner->tracks.color;
        case osbx_track_kind_t::visibility: return &owner->tracks.visibility;
        case osbx_track_kind_t::blend: return &owner->tracks.blend;
        }
        return {};
    }
    auto subtree(const std::string& root) const -> std::unordered_set<std::string> {
        std::unordered_map<std::string, std::vector<std::string>> children;
        for (const auto& item : scene.nodes)
            if (item.parent) children[*item.parent].push_back(item.id);
        std::unordered_set<std::string> found;
        std::vector<std::string> pending{root};
        while (!pending.empty()) {
            auto id = std::move(pending.back()); pending.pop_back();
            if (!found.insert(id).second) continue;
            const auto entry = children.find(id);
            if (entry != children.end()) pending.insert(pending.end(), entry->second.begin(), entry->second.end());
        }
        return found;
    }
    auto operator()(const osbx_add_node_t& edit) -> std::string {
        if (edit.kind != osbx_node_kind_t::group && edit.kind != osbx_node_kind_t::sprite)
            return "Only groups and sprites can be added";
        if (edit.parent) {
            const auto* parent = node(*edit.parent);
            if (!parent) return "Parent node does not exist";
            if (parent->children_link) return "Lua-linked children: detach first";
        }
        int order = -1;
        for (const auto& item : scene.nodes)
            if (item.layer == edit.layer) order = std::max(order, item.order);
        if (order == std::numeric_limits<int>::max()) return "Layer order is exhausted";
        changed = true;
        scene.nodes.push_back({.id = ids.inserted_node ? *ids.inserted_node : ids.next(edit.parent.value_or("scene"), "node", scene),
            .kind = edit.kind, .parent = edit.parent, .layer = edit.layer, .order = order + 1, .image = edit.image});
        return {};
    }
    auto operator()(const osbx_delete_node_t& edit) -> std::string {
        if (!node(edit.node)) return "Node does not exist";
        changed = true;
        const auto removed = subtree(edit.node);
        std::erase_if(scene.nodes, [&](const auto& item) { return removed.contains(item.id); });
        return {};
    }
    auto operator()(const osbx_rename_node_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->id != edit.new_id;
        target->id = edit.new_id;
        for (auto& item : scene.nodes)
            if (item.parent == edit.node) item.parent = edit.new_id;
        return {};
    }
    auto operator()(const osbx_reparent_node_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->parent != edit.parent;
        if (edit.parent) {
            const auto* parent = node(*edit.parent);
            if (parent && parent->children_link) return "Lua-linked children: detach first";
        }
        target->parent = edit.parent; // Validator owns existence, layer and cycle checks.
        return {};
    }
    auto operator()(const osbx_reorder_node_t& edit) -> std::string {
        const auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        std::vector<osbx_node_t*> ordered;
        for (auto& item : scene.nodes)
            if (item.layer == target->layer) ordered.push_back(&item);
        if (edit.index >= ordered.size()) return "Layer order index is out of range";
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->order < b->order; });
        const auto position = std::ranges::find(ordered, target);
        auto* moved = *position;
        ordered.erase(position);
        ordered.insert(ordered.begin() + static_cast<std::ptrdiff_t>(edit.index), moved);
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            changed = changed || ordered[index]->order != static_cast<int>(index);
            ordered[index]->order = static_cast<int>(index);
        }
        return {};
    }
    auto operator()(const osbx_set_order_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->order != edit.order;
        target->order = edit.order;
        return {};
    }
    auto operator()(const osbx_duplicate_node_t& edit) -> std::string {
        const auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        if (auto reason = protected_children_reason(*target); !reason.empty()) return reason;
        const auto selected = subtree(edit.node);
        auto original = layer_order(target->layer);
        std::vector<std::string_view> duplicated(selected.begin(), selected.end());
        if (!osbx_clip_expansion_cost_t::from(scene, {}, duplicated).value)
            return "Duplicate subtree exceeds aggregate program/clip limits before copying";
        auto copies = clone_subtree(*target, selected);
        std::vector<std::string> original_ids;
        for (const auto* item : original) original_ids.push_back(item->id);
        const auto last = std::find_if(original_ids.rbegin(), original_ids.rend(),
            [&](const auto& id) { return selected.contains(id); }).base();
        const auto offset = static_cast<std::size_t>(last - original_ids.begin());
        std::unordered_map<std::string, std::string> copied_ids;
        for (const auto& [source, copy] : copies) copied_ids.emplace(source, copy.id);
        for (auto& [source, copy] : copies) { (void)source; scene.nodes.push_back(std::move(copy)); }
        std::vector<std::string> inserted;
        for (const auto& id : original_ids)
            if (const auto found = copied_ids.find(id); found != copied_ids.end()) inserted.push_back(found->second);
        original_ids.insert(original_ids.begin() + static_cast<std::ptrdiff_t>(offset), inserted.begin(), inserted.end());
        for (std::size_t i = 0; i < original_ids.size(); ++i) node(original_ids[i])->order = static_cast<int>(i);
        changed = true;
        return {};
    }
    auto operator()(const osbx_drop_node_t& edit) -> std::string {
        auto* source = node(edit.node);
        if (!source) return "Node does not exist";
        const auto moved = subtree(edit.node);
        if (auto failure = validate_drop(edit, *source, moved); !failure.empty()) return failure;
        auto original = layer_order(source->layer);
        std::vector<osbx_node_t*> remaining, block;
        for (auto* item : original) (moved.contains(item->id) ? block : remaining).push_back(item);
        const auto offset = drop_offset(edit, remaining);
        remaining.insert(remaining.begin() + static_cast<std::ptrdiff_t>(offset), block.begin(), block.end());
        changed = source->parent != edit.parent || original != remaining;
        if (!changed) return {};
        source->parent = edit.parent;
        for (std::size_t i = 0; i < remaining.size(); ++i) remaining[i]->order = static_cast<int>(i);
        return {};
    }
    auto operator()(const osbx_set_layer_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        if (target->parent) {
            const auto* parent = node(*target->parent);
            if (parent && parent->layer != edit.layer) { target->parent.reset(); changed = true; }
        }
        const auto moved = subtree(edit.node);
        for (auto& item : scene.nodes)
            if (moved.contains(item.id)) {
                changed = changed || item.layer != edit.layer;
                item.layer = edit.layer;
            }
        return {};
    }
    auto operator()(const osbx_set_image_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->image != edit.image;
        target->image = edit.image;
        return {};
    }
    auto operator()(const osbx_set_origin_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->origin != edit.origin;
        target->origin = edit.origin;
        return {};
    }
    auto operator()(const osbx_set_lifetime_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        changed = target->lifetime != edit.lifetime;
        target->lifetime = edit.lifetime;
        return {};
    }
    auto operator()(const osbx_add_op_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target) return "Node does not exist";
        auto* stack = &target->ops;
        if (edit.parent_op) {
            auto* parent = transform(*target, *edit.parent_op);
            if (!parent || parent->kind != osbx_transform_kind_t::pivot) return "Parent op must be a pivot";
            stack = &parent->children;
        }
        osbx_transform_op_t op{.id = ids.next(edit.parent_op.value_or(edit.node), "op", scene), .kind = edit.kind};
        switch (edit.kind) {
        case osbx_transform_kind_t::rotate: op.track.default_value = {0}; break;
        case osbx_transform_kind_t::scale: op.track.default_value = {1, 1}; break;
        case osbx_transform_kind_t::matrix: op.track.default_value = {1, 0, 0, 1, 0, 0}; break;
        case osbx_transform_kind_t::translate:
        case osbx_transform_kind_t::skew:
        case osbx_transform_kind_t::pivot: break;
        }
        changed = true;
        stack->push_back(std::move(op));
        return {};
    }
    auto operator()(const osbx_remove_op_t& edit) -> std::string {
        auto* target = node(edit.node);
        auto* stack = target ? find_op_stack(target->ops, edit.op) : nullptr;
        if (!stack) return "Transform op does not exist";
        changed = true;
        std::erase_if(*stack, [&](const auto& op) { return op.id == edit.op; });
        return {};
    }
    auto operator()(const osbx_reorder_op_t& edit) -> std::string {
        auto* target = node(edit.node);
        auto* stack = target ? find_op_stack(target->ops, edit.op) : nullptr;
        if (!stack) return "Transform op does not exist";
        if (edit.index >= stack->size()) return "Transform order index is out of range";
        const auto position = std::ranges::find(*stack, edit.op, &osbx_transform_op_t::id);
        changed = static_cast<std::size_t>(position - stack->begin()) != edit.index;
        auto moved = std::move(*position);
        stack->erase(position);
        stack->insert(stack->begin() + static_cast<std::ptrdiff_t>(edit.index), std::move(moved));
        return {};
    }
    template<class Value> auto set_value(Value& destination, const osbx_edit_value_t& value) -> std::string {
        const auto* typed = std::get_if<Value>(&value);
        if (!typed) return "Value type does not match the track";
        changed = changed || destination != *typed;
        destination = *typed;
        return {};
    }
    template<class Value> auto edit_track(osbx_track_t<Value>& target, const osbx_set_default_t& edit) -> std::string {
        if (target.lua) return "Lua-linked defaults cannot be edited: bake or unlink first";
        return set_value(target.default_value, edit.value);
    }
    template<class Value> auto edit_track(osbx_track_t<Value>& target, const osbx_add_key_t& edit) -> std::string {
        if (!target.can_edit_key_at(edit.time_ms)) return "Linked to Lua: bake or unlink to edit";
        osbx_key_t<Value> key{.id = ids.next(edit.track.kind == osbx_track_kind_t::transform ? edit.track.op : edit.track.node,
            "key", scene), .time_ms = edit.time_ms, .easing = edit.easing};
        if (auto failure = set_value(key.value, edit.value); !failure.empty()) return failure;
        changed = true;
        target.keys.push_back(std::move(key));
        return {};
    }
    template<class Value> auto edit_key(osbx_key_t<Value>& target, const osbx_move_key_t& edit) -> std::string {
        changed = changed || target.time_ms != edit.time_ms;
        target.time_ms = edit.time_ms;
        return {};
    }
    template<class Value> auto edit_key(osbx_key_t<Value>& target, const osbx_set_key_value_t& edit) -> std::string {
        return set_value(target.value, edit.value);
    }
    template<class Value> auto edit_key(osbx_key_t<Value>& target, const osbx_set_easing_t& edit) -> std::string {
        changed = target.easing != edit.easing;
        target.easing = edit.easing;
        return {};
    }
    template<class Value, class Edit> auto edit_track(osbx_track_t<Value>& target, const Edit& edit) -> std::string {
        const auto found = std::ranges::find(target.keys, edit.key, &osbx_key_t<Value>::id);
        if (found == target.keys.end()) return "Key does not exist on this track";
        if (!target.can_edit_key_at(found->time_ms)) return "Linked to Lua: bake or unlink to edit";
        if constexpr (std::same_as<Edit, osbx_move_key_t>)
            if (!target.can_edit_key_at(edit.time_ms)) return "Linked to Lua: bake or unlink to edit";
        if constexpr (std::same_as<Edit, osbx_delete_key_t>) {
            changed = true;
            target.keys.erase(found);
            return {};
        } else return edit_key(*found, edit);
    }
    auto operator()(const osbx_bake_spline_t& edit) -> std::string {
        auto* owner = node(edit.track.node);
        auto* op = owner ? transform(*owner, edit.track.op) : nullptr;
        if (edit.track.kind != osbx_track_kind_t::transform || !op)
            return "Bezier requires an existing transform";
        const bool scalar = op->kind == osbx_transform_kind_t::rotate;
        if ((!scalar && op->kind != osbx_transform_kind_t::translate && op->kind != osbx_transform_kind_t::scale) ||
            edit.spline.dimensions != (scalar ? 1u : 2u)) return "Bezier requires translation, rotation or scale with matching dimensions";
        if (op->track.lua || !op->track.program.empty()) return "Bake or unlink scripted/program tracks before adding a Bezier";
        const auto baked = edit.spline.bake();
        if (!baked) return baked.error().message;
        const auto allocated = ids.next_batch(op->id, "key", scene, baked->size());
        if (allocated.size() != baked->size()) return "Storyboard key IDs are exhausted";
        std::erase_if(op->track.keys, [&](const auto& key) {
            return key.time_ms >= edit.spline.begin_ms && key.time_ms <= edit.spline.end_ms;
        });
        for (std::size_t i = 0; i < baked->size(); ++i) {
            const auto& key = (*baked)[i];
            op->track.keys.push_back({.id = allocated[i], .time_ms = key.time_ms,
                .value = std::vector<double>(key.value.begin(), key.value.begin() + edit.spline.dimensions)});
        }
        changed = true;
        return {};
    }
    template<class Edit> requires requires(const Edit& edit) { edit.track; }
    auto operator()(const Edit& edit) -> std::string {
        const auto target = track(edit.track);
        if (!target) return "Track does not exist";
        return std::visit([&](auto* value) -> std::string {
            return edit_track(*value, edit);
        }, *target);
    }
    auto operator()(const osbx_delete_keys_t& edit) -> std::string {
        std::unordered_set<std::string> seen;
        for (const auto& item : edit.keys) {
            if (!seen.insert(item.key).second) return "Key selection contains duplicate IDs";
            if (auto failure = (*this)(osbx_delete_key_t{.track = item.track, .key = item.key}); !failure.empty())
                return failure;
        }
        return {};
    }
    auto move_selected_key(const osbx_key_target_t& item, std::int64_t delta) -> std::string {
        const auto target = track(item.track);
        if (!target) return "Track does not exist";
        const auto time = std::visit([&](const auto* value) -> std::optional<std::int64_t> {
            const auto key = std::ranges::find(value->keys, item.key, [](const auto& key) { return key.id; });
            if (key == value->keys.end()) return {};
            return checked_math::checked_add(key->time_ms, delta);
        }, *target);
        if (!time) return "Key does not exist or its destination overflows integer milliseconds";
        return (*this)(osbx_move_key_t{.track = item.track, .key = item.key, .time_ms = *time});
    }
    auto operator()(const osbx_move_keys_t& edit) -> std::string {
        std::unordered_set<std::string> seen;
        for (const auto& item : edit.keys) {
            if (!seen.insert(item.key).second) return "Key selection contains duplicate IDs";
            if (auto failure = move_selected_key(item, edit.delta_ms); !failure.empty()) return failure;
        }
        return {};
    }
    auto operator()(const osbx_set_lua_track_t& edit) -> std::string {
        auto* owner = node(edit.track.node);
        if (!owner) return "Node does not exist";
        if (auto reason = protected_children_reason(*owner); !reason.empty()) return reason;
        if (edit.track.kind == osbx_track_kind_t::visibility || edit.track.kind == osbx_track_kind_t::blend)
            return "Only numeric tracks support Lua links";
        if (edit.track.kind == osbx_track_kind_t::transform) {
            const auto* op = transform(*owner, edit.track.op);
            if (!op || op->kind == osbx_transform_kind_t::pivot) return "Pivot anchors cannot be scripted";
        }
        const auto target = track(edit.track);
        if (!target) return "Track does not exist";
        auto* numeric = std::get_if<osbx_track_t<>*>(&*target);
        if (!numeric) return "Only numeric tracks support Lua links";
        auto link = edit.link;
        const auto& current = (**numeric).lua;
        const auto id = current ? current->link.id : std::string{};
        if (id != edit.expected_link) return "Lua link identity changed";
        if (link) link->link.id = current ? id : ids.next(edit.track.node, "link", scene);
        changed = current != link;
        (**numeric).lua = std::move(link);
        return {};
    }
    auto operator()(const osbx_set_lua_children_t& edit) -> std::string {
        auto* target = node(edit.node);
        if (!target || target->kind != osbx_node_kind_t::group) return "Only groups support scripted children";
        if (auto reason = protected_children_reason(*target); !reason.empty()) return reason;
        const auto id = target->children_link ? target->children_link->id : std::string{};
        if (id != edit.expected_link) return "Lua link identity changed";
        if (edit.link && std::ranges::any_of(scene.nodes, [&](const auto& child) { return child.parent == edit.node; }))
            return "A scripted group cannot replace authored children";
        auto link = edit.link;
        if (link) link->id = target->children_link ? id : ids.next(edit.node, "link", scene);
        changed = target->children_link != link;
        target->children_link = std::move(link);
        return {};
    }
    auto operator()(const osbx_add_image_t& edit) -> std::string {
        changed = true;
        scene.images.push_back({.id = ids.next("scene", "image", scene), .file = edit.file});
        return {};
    }
    auto operator()(const osbx_place_image_t& edit) -> std::string {
        std::string image;
        if (const auto* existing = std::get_if<osbx_existing_image_t>(&edit.source)) {
            image = existing->image;
            if (std::ranges::find(scene.images, image, &osbx_image_t::id) == scene.images.end())
                return "Storyboard image no longer exists";
        } else {
            if (!ids.inserted_image) return "Image identity is exhausted";
            image = *ids.inserted_image;
            const auto file = std::visit([](const auto& source) -> std::string {
                using T = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<T, osbx_resource_image_t>) return source.file;
                else if constexpr (std::is_same_v<T, osbx_import_image_t>)
                    return source.file.copied_project_relative_path.value_or(std::filesystem::path{}).generic_string();
                else return {};
            }, edit.source);
            if (!osbx_document_t::is_valid_project_path(file)) return "Image requires a safe project-relative path";
            scene.images.push_back({.id = image, .file = file}); changed = true;
        }
        if (!edit.sprite) return {};
        if (!std::isfinite(edit.sprite->x) || !std::isfinite(edit.sprite->y)) return "Invalid sprite position";
        const auto failure = (*this)(osbx_add_node_t{.kind = osbx_node_kind_t::sprite,
            .layer = edit.sprite->layer, .image = image});
        if (!failure.empty()) return failure;
        if (!ids.inserted_node || !ids.inserted_op) return "Sprite identity is exhausted";
        node(*ids.inserted_node)->ops.push_back({.id = *ids.inserted_op, .kind = osbx_transform_kind_t::translate,
            .track = {.default_value = {edit.sprite->x, edit.sprite->y}}});
        return {};
    }
    auto operator()(const osbx_remove_image_t& edit) -> std::string {
        if (std::erase_if(scene.images, [&](const auto& image) { return image.id == edit.image; }) == 0)
            return "Image asset does not exist";
        changed = true;
        return {}; // Referenced assets fail postvalidation.
    }
    template<class Edit> auto operator()(const Edit&) -> std::string { return "Not a scene edit"; }

    void sort_keys() {
        for (auto& item : scene.nodes) {
            sort_ops(item.ops);
            sort_track(item.tracks.opacity); sort_track(item.tracks.color);
            sort_track(item.tracks.visibility); sort_track(item.tracks.blend);
        }
    }
  private:
    auto protected_children_reason(const osbx_node_t& source) -> std::string {
        const auto* ancestor = &source;
        for (std::size_t i = 0; ancestor->parent && i < scene.nodes.size(); ++i) {
            ancestor = node(*ancestor->parent);
            if (!ancestor) break;
            if (ancestor->children_link) return "Lua-linked children: detach first";
        }
        return {};
    }
    auto layer_order(osbx_layer_t layer) -> std::vector<osbx_node_t*> {
        std::vector<osbx_node_t*> result;
        for (auto& item : scene.nodes) if (item.layer == layer) result.push_back(&item);
        std::stable_sort(result.begin(), result.end(), [](const auto* a, const auto* b) { return a->order < b->order; });
        return result;
    }
    auto clone_subtree(const osbx_node_t& root, const std::unordered_set<std::string>& selected)
        -> std::vector<std::pair<std::string, osbx_node_t>> {
        subtree_clone_t allocation{scene, ids};
        std::unordered_map<std::string, std::vector<const osbx_node_t*>> children;
        for (const auto& item : scene.nodes)
            if (selected.contains(item.id) && item.parent) children[*item.parent].push_back(&item);
        std::vector<std::pair<const osbx_node_t*, std::optional<std::string>>> pending{{&root, root.parent}};
        std::vector<std::pair<std::string, osbx_node_t>> result;
        std::unordered_set<std::string_view> visited;
        while (!pending.empty()) {
            auto [source, parent] = std::move(pending.back()); pending.pop_back();
            if (!visited.insert(source->id).second) continue;
            auto id = source == &root && ids.inserted_node ? *ids.inserted_node :
                allocation.reserve(parent.value_or("scene"), "node");
            for (const auto* child : children[source->id]) pending.emplace_back(child, id);
            result.emplace_back(source->id, allocation.clone(*source, std::move(id), std::move(parent)));
        }
        return result;
    }
    auto validate_drop(const osbx_drop_node_t& edit, const osbx_node_t& source,
                       const std::unordered_set<std::string>& moved) -> std::string {
        if (auto reason = protected_children_reason(source); !reason.empty()) return reason;
        if (source.layer != edit.layer) return "Cross-layer drops are refused; use Move to layer instead";
        if (edit.parent) {
            const auto* parent = node(*edit.parent);
            if (!parent) return "Parent node does not exist";
            if (parent->layer != source.layer) return "Reparenting requires the same layer";
            if (moved.contains(parent->id)) return "Reparenting would create a cycle";
            if (parent->children_link) return "Lua-linked children: detach first";
        }
        if (edit.before) {
            const auto* before = node(*edit.before);
            if (!before || before->layer != source.layer || before->parent != edit.parent)
                return "Drop target must be a current sibling in the same layer";
            if (moved.contains(before->id)) return "A subtree cannot be dropped inside itself";
        }
        return {};
    }
    auto drop_offset(const osbx_drop_node_t& edit, const std::vector<osbx_node_t*>& remaining) const -> std::size_t {
        if (edit.before) {
            const auto target = subtree(*edit.before);
            for (std::size_t i = 0; i < remaining.size(); ++i)
                if (target.contains(remaining[i]->id)) return i;
            return remaining.size();
        }
        if (!edit.parent) return remaining.size();
        const auto destination = subtree(*edit.parent);
        for (std::size_t i = remaining.size(); i > 0; --i)
            if (destination.contains(remaining[i - 1]->id)) return i;
        return remaining.size();
    }
    template<class Value> static void sort_track(osbx_track_t<Value>& value) {
        std::ranges::stable_sort(value.keys, {}, &osbx_key_t<Value>::time_ms);
    }
    static void sort_ops(std::vector<osbx_transform_op_t>& ops) {
        for (auto& op : ops) { sort_track(op.track); sort_ops(op.children); }
    }
};

auto validate_scene(const osbx_document_t& scene) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& item : validate_osbx_document(scene).diagnostics)
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
            "OSBX [" + item.location.id + "]: " + item.message));
    return diagnostics;
}
auto append_storyboard(song_document_t& song, std::string name, osbx_document_t scene,
    const osbx_edit_ids_t& ids, std::string label) -> attempt_t<document_edit_effect_t> {
    if (!ids.next_storyboard || std::ranges::find(song.storyboards, *ids.next_storyboard,
            &storyboard_document_t::id) != song.storyboards.end())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Storyboard identity is exhausted or already used")}};
    if (auto diagnostics = validate_scene(scene); !diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    song.storyboards.push_back({.id = *ids.next_storyboard, .name = std::move(name), .osbx = std::move(scene)});
    return {.value = document_edit_effect_t{.changed = true, .command_label = std::move(label)}};
}
auto convert_storyboard(song_document_t& song, const storyboard_document_t& source,
    const osbx_edit_ids_t& ids) -> attempt_t<document_edit_effect_t> {
    if (!source.model || source.osbx)
        return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, "Conversion requires an OSB-model storyboard")}};
    auto converted = convert_osb_to_osbx(*source.model, ids);
    diagnostic_list_t diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics.push_back(error(diagnostic.code == failure_code_t::unsupported ?
            diagnostic_code_t::unsupported_feature : diagnostic_code_t::invalid_binding,
            diagnostic.location.id + " line " + std::to_string(diagnostic.location.provenance.line) + ": " + diagnostic.message));
    if (!converted.value) return {.diagnostics = std::move(diagnostics)};
    return append_storyboard(song, source.name + " (OSBX)", std::move(*converted.value), ids, edit_label<osbx_convert_storyboard_t>());
}
auto detach_storyboard(song_document_t& song, storyboard_document_t& board,
    const osbx_detach_storyboard_t& edit) -> attempt_t<document_edit_effect_t> {
    if (!edit.expected_owner || board.owning_generator_recipe != edit.expected_owner ||
        !board.osbx || board.model || !board.retained_source_lines.empty() ||
        std::ranges::count(song.storyboards, board.id, &storyboard_document_t::id) != 1)
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generated storyboard ownership changed or is unavailable.")}};
    if (std::ranges::count(song.generator_recipes, edit.expected_owner, &generator_recipe_t::id) != 1)
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Owning generator recipe is missing or ambiguous.")}};
    auto recipe = std::ranges::find(song.generator_recipes, edit.expected_owner, &generator_recipe_t::id);
    std::size_t claims = 0;
    for (const auto& owner : song.generator_recipes)
        claims += static_cast<std::size_t>(std::ranges::count(owner.generated_storyboards, board.id,
            &generator_storyboard_binding_t::storyboard));
    const auto binding = std::ranges::find(recipe->generated_storyboards, board.id,
        &generator_storyboard_binding_t::storyboard);
    if (claims != 1 || binding == recipe->generated_storyboards.end() || binding->value != *board.osbx)
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generated storyboard binding is missing, ambiguous or changed.")}};
    if (auto diagnostics = validate_generator_storyboards(*recipe); !diagnostics.empty())
        return {.diagnostics = std::move(diagnostics)};
    board.owning_generator_recipe.reset();
    recipe->generated_storyboards.erase(binding);
    return {.value = document_edit_effect_t{.changed = true,
        .command_label = edit_label<osbx_detach_storyboard_t>()}};
}
auto stage_image_source(document_command_transaction_t& transaction, const osbx_place_image_t& edit)
    -> attempt_t<document_edit_effect_t> {
    if (const auto* source = std::get_if<osbx_import_image_t>(&edit.source)) {
        if (source->file.role != resource_role_t::bga || !source->file.expected_new_asset_id ||
            !source->file.copied_project_relative_path || !source->file.image_geometry)
            return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Incomplete prepared image import")}};
        return stage_resource_asset_import(transaction, source->file, std::nullopt);
    }
    if (const auto* source = std::get_if<osbx_resource_image_t>(&edit.source)) {
        const auto& song = transaction.current_document();
        const auto resource = std::ranges::find(song.resources, source->resource, &resource_entry_t::id);
        const auto asset = std::ranges::find(song.visual_assets, source->asset, &visual_asset_t::id);
        if (resource == song.resources.end() || resource->asset != source->asset || asset == song.visual_assets.end() ||
            asset->kind != asset_kind_t::image || asset->source_path != source->source_path ||
            !osbx_document_t::is_valid_project_path(source->file))
            return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Image resource source changed or path is unsafe")}};
        return stage_resource_asset_relink(transaction, {.asset = source->asset, .source_path = source->file});
    }
    return {.value = document_edit_effect_t{}};
}
auto edit_storyboard_scene(storyboard_document_t& board, const osbx_edit_t& edit, const osbx_edit_ids_t& ids)
    -> attempt_t<document_edit_effect_t> {
    auto& scene = *board.osbx;
    bool changed = std::holds_alternative<osbx_delete_storyboard_t>(edit.change);
    if (const auto* rename = std::get_if<osbx_rename_storyboard_t>(&edit.change)) {
        changed = board.name != rename->name;
        board.name = rename->name;
    } else if (!std::holds_alternative<osbx_delete_storyboard_t>(edit.change)) {
        scene_edit_t mutation{.scene = scene, .ids = ids};
        const auto failure = std::visit(mutation, edit.change);
        if (!failure.empty()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding, failure)}};
        mutation.sort_keys();
        changed = mutation.changed;
    }
    if (auto diagnostics = validate_scene(scene); !diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    return {.value = document_edit_effect_t{.changed = changed, .command_label = edit_labels[edit.change.index()]}};
}
} // namespace

auto osbx_edit_ids_t::from(const document_history_t& history) -> osbx_edit_ids_t {
    osbx_edit_ids_t result;
    collect_song_ids(history.document, result.reserved);
    for (const auto* records : {&history.undo, &history.redo})
        for (const auto& record : *records) {
            collect_song_ids(record.before, result.reserved);
            collect_song_ids(record.after, result.reserved);
        }
    if (const auto next = id_ceiling_t<object_id_t>::from(history).next())
        result.next_storyboard = storyboard_id_t{next->value};
    return result;
}
auto osbx_edit_ids_t::next(std::string_view parent, std::string_view kind,
                          const osbx_document_t& scene) const -> std::string {
    auto used = reserved;
    collect_ids(scene, used);
    const auto prefix = std::string(parent) + "/" + std::string(kind) + "/";
    std::uint64_t maximum = 0;
    for (const auto& id : used) {
        if (!id.starts_with(prefix)) continue;
        std::uint64_t counter{};
        if (parse_integer(std::string_view{id}.substr(prefix.size()), counter))
            maximum = std::max(maximum, counter);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) return {};
    return prefix + std::to_string(maximum + 1);
}
auto osbx_edit_ids_t::next_batch(std::string_view parent, std::string_view kind,
    const osbx_document_t& scene, std::size_t count) const -> std::vector<std::string> {
    if (!count || count > storyboard_spline_t::maximum_keys) return {};
    const auto first = next(parent, kind, scene);
    const auto separator = first.rfind('/');
    std::uint64_t counter{};
    if (separator == std::string::npos || !parse_integer(std::string_view{first}.substr(separator + 1), counter) ||
        count - 1 > UINT64_MAX - counter) return {};
    const auto prefix = first.substr(0, separator + 1);
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) result.push_back(prefix + std::to_string(counter + i));
    return result;
}

auto osbx_edit_t::apply(document_command_transaction_t& transaction, const osbx_edit_ids_t& ids) const
    -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        if (const auto* create = std::get_if<osbx_create_storyboard_t>(&change)) {
            return append_storyboard(song, create->name, {}, ids, edit_labels.front());
        }
        const auto found = std::ranges::find(song.storyboards, storyboard, &storyboard_document_t::id);
        if (found == song.storyboards.end())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Storyboard does not exist")}};
        if (const auto* detach = std::get_if<osbx_detach_storyboard_t>(&change))
            return detach_storyboard(song, *found, *detach);
        if (found->owning_generator_recipe)
            return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generator-owned storyboard: detach first")}};
        if (std::holds_alternative<osbx_convert_storyboard_t>(change))
            return convert_storyboard(song, *found, ids);
        if (!found->osbx || found->model)
            return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, "OSBX edits require an OSBX storyboard; convert OSB first")}};
        if (const auto* place = std::get_if<osbx_place_image_t>(&change)) {
            const auto staged = stage_image_source(transaction, *place);
            if (!staged.ok() || !staged.value) return staged;
        }
        auto result = edit_storyboard_scene(*found, *this, ids);
        if (result.value && result.ok() && std::holds_alternative<osbx_delete_storyboard_t>(change)) song.storyboards.erase(found);
        return result;
    });
}
namespace {
auto planned_node_id(const song_document_t& song, const osbx_edit_t& edit, const osbx_edit_ids_t& ids)
    -> std::optional<std::string> {
    const auto board = std::ranges::find(song.storyboards, edit.storyboard, &storyboard_document_t::id);
    if (board == song.storyboards.end() || !board->osbx) return {};
    const auto& scene = *board->osbx;
    if (const auto* place = std::get_if<osbx_place_image_t>(&edit.change); place && place->sprite)
        return ids.next("scene", "node", scene);
    if (const auto* add = std::get_if<osbx_add_node_t>(&edit.change))
        return ids.next(add->parent.value_or("scene"), "node", scene);
    if (const auto* duplicate = std::get_if<osbx_duplicate_node_t>(&edit.change)) {
        const auto source = std::ranges::find(scene.nodes, duplicate->node, &osbx_node_t::id);
        if (source != scene.nodes.end()) return ids.next(source->parent.value_or("scene"), "node", scene);
    }
    return {};
}
auto execute_osbx_edit(document_history_t& history, const osbx_edit_t& edit, const osbx_edit_ids_t& ids)
    -> document_command_result_t {
    document_command_transaction_t transaction{history};
    return transaction.finish_execution(edit.apply(transaction, ids));
}
} // namespace

auto apply_osbx_edit_with_created_ids(document_history_t& history, const osbx_edit_t& edit)
    -> command_result_with_t<osbx_edit_created_ids_t> {
    const bool inserts = std::holds_alternative<osbx_create_storyboard_t>(edit.change) ||
        std::holds_alternative<osbx_convert_storyboard_t>(edit.change) ||
        std::holds_alternative<osbx_add_node_t>(edit.change) ||
        std::holds_alternative<osbx_duplicate_node_t>(edit.change) ||
        std::holds_alternative<osbx_add_op_t>(edit.change) ||
        std::holds_alternative<osbx_add_key_t>(edit.change) ||
        std::holds_alternative<osbx_bake_spline_t>(edit.change) ||
        std::holds_alternative<osbx_add_image_t>(edit.change) ||
        std::holds_alternative<osbx_place_image_t>(edit.change) ||
        std::holds_alternative<osbx_set_lua_track_t>(edit.change) ||
        std::holds_alternative<osbx_set_lua_children_t>(edit.change);
    auto ids = inserts ? osbx_edit_ids_t::from(history) : osbx_edit_ids_t{};
    ids.inserted_node = planned_node_id(history.document, edit, ids);
    if (const auto* place = std::get_if<osbx_place_image_t>(&edit.change)) {
        const auto board = std::ranges::find(history.document.storyboards, edit.storyboard, &storyboard_document_t::id);
        if (board != history.document.storyboards.end() && board->osbx) {
            if (!std::holds_alternative<osbx_existing_image_t>(place->source))
                ids.inserted_image = ids.next("scene", "image", *board->osbx);
            if (ids.inserted_node) ids.inserted_op = ids.next(*ids.inserted_node, "op", *board->osbx);
        }
    }
    auto command = execute_osbx_edit(history, edit, ids);
    const bool creates = std::holds_alternative<osbx_create_storyboard_t>(edit.change) ||
        std::holds_alternative<osbx_convert_storyboard_t>(edit.change);
    osbx_edit_created_ids_t payload;
    if (command.applied) {
        if (creates) payload.storyboard = ids.next_storyboard;
        payload.node = std::move(ids.inserted_node);
        payload.image = std::move(ids.inserted_image);
    }
    return command_result_with_t<osbx_edit_created_ids_t>::from(std::move(command), std::move(payload));
}
auto apply_osbx_edit_with_result(document_history_t& history, const osbx_edit_t& edit)
    -> command_result_with_t<std::optional<storyboard_id_t>> {
    auto result = apply_osbx_edit_with_created_ids(history, edit);
    return command_result_with_t<std::optional<storyboard_id_t>>::from(
        std::move(result.command), result.payload.storyboard);
}
auto apply_osbx_edit(document_history_t& history, const osbx_edit_t& edit) -> document_command_result_t {
    return apply_osbx_edit_with_created_ids(history, edit).command;
}
namespace {
auto is_drag_change(const osbx_edit_change_t& change) -> bool {
    return std::holds_alternative<osbx_move_keys_t>(change) ||
        std::holds_alternative<osbx_move_key_t>(change) ||
        std::holds_alternative<osbx_set_default_t>(change) ||
        std::holds_alternative<osbx_set_key_value_t>(change) ||
        std::holds_alternative<osbx_add_key_t>(change) ||
        std::holds_alternative<osbx_set_order_t>(change) ||
        std::holds_alternative<osbx_set_lifetime_t>(change);
}
auto has_same_drag_target(const osbx_edit_change_t& before, const osbx_edit_change_t& after) -> bool {
    return std::visit([&](const auto& previous) {
        using edit_type = std::remove_cvref_t<decltype(previous)>;
        const auto* revised = std::get_if<edit_type>(&after);
        if (!revised) return false;
        if constexpr (std::same_as<edit_type, osbx_add_key_t>)
            return previous.track == revised->track && previous.time_ms == revised->time_ms;
        else if constexpr (std::same_as<edit_type, osbx_set_order_t> || std::same_as<edit_type, osbx_set_lifetime_t>)
            return previous.node == revised->node;
        else if constexpr (std::same_as<edit_type, osbx_move_keys_t>) return previous.keys == revised->keys;
        else if constexpr (std::same_as<edit_type, osbx_set_default_t>) return previous.track == revised->track;
        else if constexpr (std::same_as<edit_type, osbx_move_key_t> || std::same_as<edit_type, osbx_set_key_value_t>)
            return previous.track == revised->track && previous.key == revised->key;
        else return false;
    }, before);
}
auto build_prepared_preview(const song_document_t& committed, const song_document_t& candidate,
    storyboard_id_t id) -> result_t<std::vector<osbx_lua_prepared_scene_t>, failure_code_t> {
    const auto source = std::ranges::find(committed.storyboards, id, &storyboard_document_t::id);
    const auto target = std::ranges::find(candidate.storyboards, id, &storyboard_document_t::id);
    if (source == committed.storyboards.end() || target == candidate.storyboards.end() || !source->osbx || !target->osbx)
        return std::unexpected(failure_code_t::not_found);
    if (!has_osbx_lua_links(*source->osbx)) return std::vector<osbx_lua_prepared_scene_t>{};
    auto prepared = osbx_lua_prepared_scope_t::rebase(*source->osbx, *target->osbx, document_stamp_t::from(committed), id);
    if (!prepared) return std::unexpected(prepared.error());
    std::vector<osbx_lua_prepared_scene_t> result;
    result.push_back(std::move(*prepared));
    return result;
}
auto preview_drag(const editor_document_context_t& context, const osbx_edit_t& edit,
    const osbx_edit_ids_t& ids) -> diagnosed_t<std::shared_ptr<const osbx_edit_preview_t>> {
    std::shared_ptr<const osbx_edit_preview_t> retained;
    auto result = document_command_transaction_t::preview(context.history, [&](document_history_t& snapshot) {
        auto command = execute_osbx_edit(snapshot, edit, ids);
        if (diagnostic_queries::has_errors(command.diagnostics)) return command;
        auto prepared = build_prepared_preview(context.history.document, snapshot.document, edit.storyboard);
        if (!prepared) {
            command.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                "Storyboard preview cannot retain its prepared Lua source."));
            return command;
        }
        retained = std::make_shared<const osbx_edit_preview_t>(osbx_edit_preview_t{
            .capture = document_stamp_t::from(context.history.document),
            .prepared_generation = osbx_lua_prepared_scope_t::generation(), .storyboard = edit.storyboard,
            .document = std::move(snapshot.document), .prepared = std::move(*prepared)});
        return command;
    });
    return {.value = std::move(retained), .diagnostics = std::move(result.diagnostics)};
}
} // namespace

auto pending_osbx_edit_gesture_t::begin(editor_document_context_t& context, const osbx_edit_t& edit)
    -> diagnosed_t<std::optional<pending_osbx_edit_gesture_t>> {
    if (!is_drag_change(edit.change))
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "This storyboard edit cannot be dragged")}};
    auto ids = std::holds_alternative<osbx_add_key_t>(edit.change)
        ? osbx_edit_ids_t::from(context.history) : osbx_edit_ids_t{};
    auto preview = preview_drag(context, edit, ids);
    if (!preview.value) return {.diagnostics = std::move(preview.diagnostics)};
    return {.value = pending_osbx_edit_gesture_t{.edit = edit, .ids = std::move(ids), .preview_owner = std::move(preview.value)},
            .diagnostics = std::move(preview.diagnostics)};
}
auto pending_osbx_edit_gesture_t::revise(editor_document_context_t& context,
                                        const osbx_edit_change_t& change) -> diagnostic_list_t {
    if (!has_same_drag_target(edit.change, change))
        return {error(diagnostic_code_t::invalid_binding, "A storyboard drag cannot change its captured target")};
    if (change == edit.change && preview_owner &&
        preview_owner->prepared_generation == osbx_lua_prepared_scope_t::generation()) return {};
    const osbx_edit_t candidate{.storyboard = edit.storyboard, .change = change};
    auto preview = preview_drag(context, candidate, ids);
    if (preview.value) { edit.change = change; preview_owner = std::move(preview.value); }
    return std::move(preview.diagnostics);
}
auto pending_osbx_edit_gesture_t::apply(editor_document_context_t& context) const -> editor_interaction_result_t {
    return run_document_gesture_edit(context, [&](document_command_transaction_t& transaction) {
        return edit.apply(transaction, ids);
    });
}

} // namespace wormedit

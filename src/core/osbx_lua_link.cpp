#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/osbx_evaluator.h>
#include "text_util.h"
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/sv_recipe.h>
#include <algorithm>
#include <type_traits>
#include <cmath>
#include <set>
#include <map>

namespace wormedit {
namespace {
thread_local const osbx_lua_prepared_scope_t* current_scope{};
using numeric_track_t = osbx_track_t<>;
constexpr std::int64_t maximum_materialized_span = 65'535;


auto first_link(std::span<const osbx_transform_op_t> ops) -> const osbx_lua_link_t* {
    std::vector<std::span<const osbx_transform_op_t>> pending{ops};
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        for (const auto& op : current) {
            if (op.track.lua) return &op.track.lua->link;
            if (!op.children.empty()) pending.emplace_back(op.children);
        }
    }
    return nullptr;
}
auto node_link(const osbx_node_t& node) -> const osbx_lua_link_t* {
    if (node.children_link) return &*node.children_link;
    if (node.tracks.opacity.lua) return &node.tracks.opacity.lua->link;
    if (node.tracks.color.lua) return &node.tracks.color.lua->link;
    if (node.tracks.visibility.lua) return &node.tracks.visibility.lua->link;
    if (node.tracks.blend.lua) return &node.tracks.blend.lua->link;
    return first_link(node.ops);
}

auto find_op(std::vector<osbx_transform_op_t>& ops, std::string_view id) -> osbx_transform_op_t* {
    for (auto& op : ops) {
        if (op.id == id) return &op;
        if (auto* found = find_op(op.children, id)) return found;
    }
    return nullptr;
}

auto target_track(osbx_node_t& node, std::string_view target) -> numeric_track_t* {
    if (target == "opacity") return &node.tracks.opacity;
    if (target == "color") return &node.tracks.color;
    if (target.starts_with("op/") )
        if (auto* op = find_op(node.ops, target.substr(3))) return &op->track;
    return nullptr;
}

auto scoped_id(const osbx_lua_prepared_link_t& link, std::string_view local) -> std::string {
    return link.node_id + "/lua/" + std::to_string(link.link.id.size()) + ":" + link.link.id + "/" + std::string(local);
}

void scope_ops(std::vector<osbx_transform_op_t>& ops, const osbx_lua_prepared_link_t& link) {
    for (auto& op : ops) {
        op.id = scoped_id(link, op.id);
        op.track.visit_authored_ids([&](auto& id) { id = scoped_id(link, id); });
        scope_ops(op.children, link);
    }
}
template<class Value> void scope_keys(osbx_track_t<Value>& track, const osbx_lua_prepared_link_t& link) {
    track.visit_authored_ids([&](auto& id) { id = scoped_id(link, id); });
}
void scope_uses(std::vector<osbx_clip_instance_t>& uses, const osbx_lua_prepared_link_t& link) {
    for (auto& use : uses) {
        use.id = scoped_id(link, use.id);
        use.clip = scoped_id(link, use.clip);
    }
}
void append_clips(osbx_document_t& scene, const osbx_lua_prepared_link_t& link) {
    for (auto clip : link.children.clips) {
        clip.id = scoped_id(link, clip.id);
        scope_ops(clip.ops, link);
        scope_keys(clip.tracks.opacity, link); scope_keys(clip.tracks.color, link);
        scope_keys(clip.tracks.visibility, link);
        if (clip.tracks.blend) scope_keys(*clip.tracks.blend, link);
        scope_uses(clip.uses, link);
        scene.clips.push_back(std::move(clip));
    }
}
// Charge scoping growth before copying generated payloads. IDs/references
// receive the existing deterministic prefix; programs use the track visitor.
struct prepared_scope_budget_t {
    std::size_t prefix{}, growth{};
    bool valid{true};
    void id(std::string_view value) {
        const osbx_validation_options_t limits;
        if (prefix > limits.maximum_id_bytes || value.size() > limits.maximum_id_bytes - prefix ||
            growth > limits.maximum_expanded_storage_bytes - prefix) { valid = false; return; }
        growth += prefix;
    }
    template<class Value> void track(const osbx_track_t<Value>& value) {
        value.visit_authored_ids([&](const auto& value_id) { id(value_id); });
    }
    template<class Tracks> void properties(const Tracks& value) {
        track(value.opacity); track(value.color); track(value.visibility);
        if constexpr (std::is_same_v<Tracks, osbx_tracks_t>) track(value.blend);
        else if (value.blend) track(*value.blend);
    }
    void ops(std::span<const osbx_transform_op_t> value) {
        for (const auto& op : value) { id(op.id); track(op.track); ops(op.children); }
    }
    void uses(std::span<const osbx_clip_instance_t> value) {
        for (const auto& use : value) { id(use.id); id(use.clip); }
    }
    auto from(const osbx_lua_prepared_link_t& link) -> std::optional<std::size_t> {
        prefix = scoped_id(link, {}).size();
        for (const auto& image : link.children.images) id(image.id);
        for (const auto& node : link.children.nodes) {
            id(node.id); if (node.parent) id(*node.parent); if (node.image) id(*node.image);
            ops(node.ops); properties(node.tracks); uses(node.uses);
        }
        for (const auto& clip : link.children.clips) { id(clip.id); ops(clip.ops); properties(clip.tracks); uses(clip.uses); }
        return valid ? std::optional{growth} : std::nullopt;
    }
};
auto can_append_clip_children(const osbx_document_t& scene, const osbx_lua_prepared_link_t& link) -> bool {
    const auto& children = link.children;
    const osbx_validation_options_t limits;
    const auto fits = [](std::size_t a, std::size_t b, std::size_t maximum) { return a <= maximum && b <= maximum - a; };
    if (!fits(scene.nodes.size(), children.nodes.size(), limits.maximum_nodes) ||
        !fits(scene.images.size(), children.images.size(), limits.maximum_images) ||
        !fits(scene.clips.size(), children.clips.size(), limits.maximum_nodes)) return false;
    const auto a = osbx_clip_expansion_cost_t::from(scene), b = osbx_clip_expansion_cost_t::from(children);
    if (!a.value || !b.value) return false;
    const auto growth = prepared_scope_budget_t{}.from(link);
    if (!growth || !fits(b.value->storage, *growth, limits.maximum_expanded_storage_bytes)) return false;
    return fits(a.value->ops, b.value->ops, limits.maximum_ops) &&
        fits(a.value->keys, b.value->keys, limits.maximum_keys) &&
        fits(a.value->layers, b.value->layers, limits.maximum_expanded_layers) &&
        fits(a.value->storage, b.value->storage + *growth, limits.maximum_expanded_storage_bytes);
}
auto append_children(osbx_document_t& scene, const osbx_lua_prepared_link_t& link, osbx_layer_t layer) -> bool {
    if (!can_append_clip_children(scene, link)) return false;
    append_clips(scene, link);
    for (auto image : link.children.images) {
        image.id = scoped_id(link, image.id);
        scene.images.push_back(std::move(image));
    }
    for (auto child : link.children.nodes) {
        child.id = scoped_id(link, child.id);
        child.parent = child.parent ? scoped_id(link, *child.parent) : link.node_id;
        child.layer = layer;
        if (child.image) child.image = scoped_id(link, *child.image);
        scope_ops(child.ops, link);
        scope_keys(child.tracks.opacity, link);
        scope_keys(child.tracks.color, link);
        scope_keys(child.tracks.visibility, link);
        scope_keys(child.tracks.blend, link);
        scope_uses(child.uses, link);
        scene.nodes.push_back(std::move(child));
    }
    return true;
}

using key_map_t = std::map<std::int64_t, osbx_key_t<>>;
auto generated_key(const numeric_track_t& source, const osbx_lua_prepared_link_t& prepared, std::int64_t time)
    -> osbx_key_t<> {
    return {.id = scoped_id(prepared, "sample/" + std::to_string(time)), .time_ms = time,
        .value = sample_osbx_numeric_track(source, time)};
}
auto is_linear_or_step(const osbx_easing_t& easing) -> bool {
    return std::holds_alternative<osbx_linear_easing_t>(easing) ||
        std::holds_alternative<osbx_step_easing_t>(easing);
}
// A nonlinear authored segment cannot generally be cut into two equal easings.
// Preserve it at every integer-ms tick where a range boundary cuts that segment.
auto preserve_guard(key_map_t& keys, const numeric_track_t& authored,
    const osbx_lua_prepared_link_t& prepared, std::int64_t guard, bool before) -> std::string {
    keys[guard] = generated_key(authored, prepared, guard);
    const auto upper = std::ranges::upper_bound(authored.keys, guard, {}, &osbx_key_t<>::time_ms);
    if (upper == authored.keys.begin() || upper == authored.keys.end()) return {};
    const auto& left = *std::prev(upper);
    if (!before && left.time_ms == guard) { keys[guard] = left; return {}; }
    if (is_linear_or_step(left.easing)) {
        if (!before) keys[guard].easing = left.easing;
        return {};
    }
    const auto begin = before ? left.time_ms : guard;
    const auto end = before ? guard : upper->time_ms;
    if (static_cast<long double>(end) - begin > maximum_materialized_span)
        return "Authored nonlinear boundary span exceeds 65536 integer-ms guard keys";
    for (auto time = begin;; ++time) {
        if (time == end && !before) break; // Retain the right key's next easing.
        keys[time] = generated_key(authored, prepared, time);
        if (time == end) break;
    }
    return {};
}
auto materialize_track(numeric_track_t& track, const osbx_lua_prepared_link_t& prepared) -> std::string {
    const auto& link = *prepared.track_link;
    if (prepared.samples.size() < 2) return "Lua track preparation has no samples";
    key_map_t keys;
    for (const auto& key : track.keys)
        if (key.time_ms < link.begin_ms || key.time_ms > link.end_ms) keys.emplace(key.time_ms, key);
    if (auto error = preserve_guard(keys, track, prepared, link.begin_ms - 1, true); !error.empty()) return error;
    if (auto error = preserve_guard(keys, track, prepared, link.end_ms + 1, false); !error.empty()) return error;
    for (auto key : prepared.samples) {
        key.id = scoped_id(prepared, "sample/" + std::to_string(key.time_ms));
        keys[key.time_ms] = std::move(key);
    }
    track.keys.clear();
    for (auto& [time, key] : keys) {
        (void)time;
        track.keys.push_back(std::move(key));
    }
    track.lua.reset();
    return {};
}

class scene_builder_t {
  public:
    osbx_lua_prepared_scene_t result;
    osbx_document_t scene;
    const osbx_lua_backend_t* backend{};
    const osbx_lua_prepared_scene_t* previous{};
    bool retained_only{};

    void diagnostic(const osbx_lua_prepared_link_t& link, std::string message) {
        const auto file = link.link.source.source_reference ? text_util::path_utf8(*link.link.source.source_reference) : "Lua link";
        std::size_t line = 1;
        const auto position = message.find(file + ":");
        if (position != std::string::npos) {
            const auto begin = position + file.size() + 1;
            const auto end = message.find(':', begin);
            if (end != std::string::npos) (void)parse_integer(std::string_view{message}.substr(begin,end-begin),line);
        } else message = file + ":1: " + message;
        result.diagnostics.push_back({.code = osbx_diagnostic_code_t::invalid_lua_link,
            .message = std::move(message),
            .location = {.id = link.node_id, .provenance = {.file = file, .line = line}, .link_id = link.link.id}});
    }
    auto validate_prepared(const osbx_lua_prepared_link_t& link) -> diagnostic_list_t {
        osbx_document_t trial;
        if (link.track_link) {
            if (link.samples.size() < 2 || link.samples.front().time_ms != link.track_link->begin_ms ||
                link.samples.back().time_ms != link.track_link->end_ms)
                return {error(diagnostic_code_t::invalid_binding,"Lua track samples do not cover their range")};
            auto& source = *std::ranges::find(result.source.nodes, link.node_id, &osbx_node_t::id);
            osbx_node_t node{.id = source.id};
            if (link.target.starts_with("op/")) node.ops.push_back(*find_op(source.ops, link.target.substr(3)));
            auto* track = target_track(node,link.target);
            track->keys = link.samples;
            track->lua.reset();
            trial.nodes.push_back(std::move(node));
        } else {
            if (!can_append_clip_children(scene, link))
                return {error(diagnostic_code_t::invalid_binding, "Prepared clip children exceed aggregate expansion limits")};
            trial = scene;
            auto& node = *std::ranges::find(trial.nodes,link.node_id,&osbx_node_t::id);
            const auto layer = node.layer;
            node.children_link.reset();
            if (!append_children(trial,link,layer))
                return {error(diagnostic_code_t::invalid_binding, "Prepared clip children exceed aggregate expansion limits")};
        }
        diagnostic_list_t errors;
        for (const auto& d : validate_osbx_document(trial).diagnostics)
            errors.push_back(error(diagnostic_code_t::invalid_binding,d.message));
        return errors;
    }

    auto retain_previous(osbx_lua_prepared_link_t link) const -> std::optional<osbx_lua_prepared_link_t> {
        if (!previous || previous->stamp.project != result.stamp.project) return std::nullopt;
        const auto found = std::ranges::find_if(previous->links, [&](const auto& old) {
            return old.node_id == link.node_id && old.target == link.target && old.link.id == link.link.id;
        });
        if (found == previous->links.end()) return std::nullopt;
        // Retain the successful payload under the current descriptor identity.
        // The old track range remains part of that visible last-good result.
        link.is_last_good = true;
        link.samples = found->samples;
        link.children = found->children;
        if (link.track_link) link.track_link = found->track_link;
        return link;
    }

    auto installed_link(const osbx_lua_prepared_link_t& link) -> std::optional<osbx_lua_prepared_link_t> {
        const auto retained = std::ranges::find_if(previous->links, [&](const auto& old) {
            return old.node_id == link.node_id && old.target == link.target;
        });
        if (retained != previous->links.end()) return *retained;
        result.complete = false;
        return std::nullopt;
    }
    auto cached_link(const osbx_lua_prepared_link_t& link, std::size_t arity) const -> std::optional<osbx_lua_prepared_link_t> {
        if (!previous || previous->stamp.project != result.stamp.project) return {};
        const auto cached = std::ranges::find_if(previous->links, [&](const auto& old) {
            return !old.is_last_good && old.node_id == link.node_id && old.target == link.target && old.link == link.link &&
                old.track_link == link.track_link &&
                (!link.track_link || (!old.samples.empty() && old.samples.front().value.size() == arity));
        });
        return cached == previous->links.end() ? std::nullopt : std::optional{*cached};
    }
    auto prepare(osbx_lua_prepared_link_t link, std::size_t arity) -> std::optional<osbx_lua_prepared_link_t> {
        if (retained_only) return installed_link(link);
        if (auto cached = cached_link(link, arity)) return cached;
        diagnostic_list_t errors;
        if (link.track_link) {
            auto prepared = backend->prepare_track(*link.track_link, arity);
            errors = std::move(prepared.diagnostics);
            link.samples = std::move(prepared.value);
        } else {
            auto prepared = backend->prepare_children(link.link);
            errors = std::move(prepared.diagnostics);
            link.children = std::move(prepared.value);
        }
        if (errors.empty()) errors = validate_prepared(link);
        if (errors.empty()) return link;
        for (const auto& error : errors) diagnostic(link, error.message);
        if (auto retained = retain_previous(link)) return retained;
        result.complete = false;
        return std::nullopt;
    }
    void track(numeric_track_t& track, const std::string& node, std::string target) {
        if (!track.lua) return;
        auto prepared = prepare({.node_id = node, .target = std::move(target),
            .link = track.lua->link, .track_link = track.lua}, track.default_value.size());
        if (!prepared) return;
        if (auto error = materialize_track(track, *prepared); !error.empty()) {
            diagnostic(*prepared, std::move(error));
            auto retained = retain_previous(*prepared);
            if (!retained) { result.complete = false; return; }
            if (auto failure = materialize_track(track,*retained); !failure.empty()) {
                diagnostic(*retained,std::move(failure));
                result.links.push_back(std::move(*retained));
                result.complete = false;
                return;
            }
            prepared = std::move(retained);
        }
        result.links.push_back(std::move(*prepared));
    }
    void ops(std::vector<osbx_transform_op_t>& ops, const std::string& node) {
        for (auto& op : ops) {
            track(op.track, node, "op/" + op.id);
            this->ops(op.children, node);
        }
    }
    void node(std::size_t index) {
        const auto id = scene.nodes[index].id;
        ops(scene.nodes[index].ops, id);
        track(scene.nodes[index].tracks.opacity, id, "opacity");
        track(scene.nodes[index].tracks.color, id, "color");
        if (!scene.nodes[index].children_link) return;
        auto prepared = prepare({.node_id = id, .target = "children", .link = *scene.nodes[index].children_link}, 0);
        if (!prepared) return;
        const auto layer = scene.nodes[index].layer;
        scene.nodes[index].children_link.reset();
        if (!append_children(scene, *prepared, layer)) {
            diagnostic(*prepared, "Prepared clip children exceed aggregate expansion limits");
            result.complete = false; return;
        }
        result.links.push_back(std::move(*prepared));
    }
};

template<class Value>
auto is_retained_track(const osbx_track_t<Value>& before, const osbx_track_t<Value>& after) -> bool {
    // Descriptor equality is sufficient to reuse the worker's immutable Lua
    // samples. Canonical edits admit hand-key changes only outside that range;
    // build_scene rematerializes their boundary guards without invoking Lua.
    return before.lua == after.lua;
}
auto are_retained_ops(std::span<const osbx_transform_op_t> before,
    std::span<const osbx_transform_op_t> after) -> bool {
    if (before.size() != after.size()) return false;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto& a = before[i]; const auto& b = after[i];
        if (a.id != b.id || a.kind != b.kind || !is_retained_track(a.track, b.track) ||
            !are_retained_ops(a.children, b.children)) return false;
    }
    return true;
}
auto is_retained_node(const osbx_node_t& a, const osbx_node_t& b) -> bool {
    return a.id == b.id && a.kind == b.kind && a.parent == b.parent && a.layer == b.layer &&
        a.image == b.image && a.animation == b.animation && a.sample == b.sample && a.origin == b.origin && a.provenance == b.provenance && a.children_link == b.children_link &&
        are_retained_ops(a.ops, b.ops) && is_retained_track(a.tracks.opacity, b.tracks.opacity) &&
        is_retained_track(a.tracks.color, b.tracks.color) && is_retained_track(a.tracks.visibility, b.tracks.visibility) &&
        is_retained_track(a.tracks.blend, b.tracks.blend) && a.uses == b.uses && a.lifetime == b.lifetime;
}
auto is_retained_source(const osbx_document_t& a, const osbx_document_t& b) -> bool {
    if (a.api_version != b.api_version || a.canvas != b.canvas || a.images != b.images ||
        a.clips != b.clips || a.nodes.size() != b.nodes.size())
        return false;
    for (std::size_t i = 0; i < a.nodes.size(); ++i)
        if (!is_retained_node(a.nodes[i], b.nodes[i])) return false;
    return true;
}
auto build_scene(scene_builder_t builder) -> osbx_lua_prepared_scene_t {
    auto validation = validate_osbx_document(builder.result.source);
    if (!validation.value) {
        builder.result.diagnostics = std::move(validation.diagnostics);
        builder.result.complete = false;
        return std::move(builder.result);
    }
    const auto authored_count = builder.result.source.nodes.size();
    for (std::size_t i = 0; i < authored_count; ++i) builder.node(i);
    validation = validate_osbx_document(builder.scene);
    if (!validation.value) {
        builder.result.diagnostics.insert(builder.result.diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
        builder.result.complete = false;
    }
    if (builder.result.complete) builder.result.scene = std::make_shared<const osbx_document_t>(std::move(builder.scene));
    return std::move(builder.result);
}
} // namespace

auto validate_osbx_lua_link(const osbx_lua_link_t& link) -> std::string {
    const auto& source = link.source;
    if (link.id.empty() || !text_util::is_valid_bounded_text(link.id, 128)) return "Lua link needs a bounded stable ID";
    if (source.api_version != lua_source_descriptor_t::current_api_version) return "Unsupported Lua link API version";
    if (!source.entry_point.empty() && !lua_source_descriptor_t::is_valid_identifier(source.entry_point)) return "Invalid Lua entry point";
    if (!source.source_reference || !text_util::is_valid_bounded_text(text_util::path_utf8(*source.source_reference),4096) ||
        !osbx_document_t::is_valid_project_path(text_util::path_utf8(*source.source_reference)))
        return "Lua link needs a project-relative module path";
    if (link.source.frozen_source.empty() || !text_util::is_valid_bounded_text(link.source.frozen_source, lua_source_descriptor_t::maximum_frozen_source_bytes))
        return "Lua link needs 1..16384 non-NUL UTF-8 source bytes";
    if (!text_util::is_sha256_digest(source.source_hash) ||
        !text_util::ascii_iequal(source.source_hash, native_project_content_checksum(link.source.frozen_source)))
        return "Frozen Lua source does not match its SHA-256";
    if (source.parameters.size() > 256) return "Lua parameter limit exceeded";
    std::set<std::string> names;
    for (const auto& parameter : source.parameters)
        if (!lua_source_descriptor_t::is_valid_identifier(parameter.name) || !names.insert(parameter.name).second ||
            !parameter.value.denominator || !std::isfinite(to_double(parameter.value))) return "Invalid or duplicate Lua parameter";
    return {};
}
auto validate_osbx_lua_track_link(const osbx_lua_track_link_t& link) -> std::string {
    if (auto error = validate_osbx_lua_link(link.link); !error.empty()) return error;
    constexpr std::int64_t exact_clock_limit = 9'007'199'254'740'990;
    if (link.begin_ms >= link.end_ms || link.begin_ms < -exact_clock_limit || link.end_ms > exact_clock_limit)
        return "Lua track needs ordered, exactly representable millisecond endpoints";
    const auto& s = link.sampling;
    if ((s.mode != osbx_lua_sampling_mode_t::adaptive && s.mode != osbx_lua_sampling_mode_t::fixed) ||
        !std::isfinite(s.tolerance) || s.tolerance <= 0 || s.max_points < 2 || s.max_points > maximum_sv_recipe_sample_points ||
        s.fixed_point_count < 2 || s.fixed_point_count > s.max_points) return "Invalid Lua track sampling policy";
    return {};
}
auto has_osbx_lua_links(const osbx_document_t& document) -> bool {
    return std::ranges::any_of(document.nodes, [](const auto& node) {
        return node_link(node) != nullptr;
    });
}
auto osbx_lua_prepared_scene_t::from(const osbx_document_t& source, document_stamp_t stamp,
    const osbx_lua_backend_t& backend, const osbx_lua_prepared_scene_t* previous) -> osbx_lua_prepared_scene_t {
    return build_scene({.result = {.stamp = stamp, .source = source, .complete = true},
        .scene = source, .backend = &backend, .previous = previous});
}
osbx_lua_prepared_scope_t::osbx_lua_prepared_scope_t(std::span<const osbx_lua_prepared_scene_t> scenes, std::uint64_t generation)
    : scenes_(scenes), generation_(generation), previous_(current_scope) { current_scope = this; }
osbx_lua_prepared_scope_t::~osbx_lua_prepared_scope_t() { current_scope = previous_; }
auto osbx_lua_prepared_scope_t::find(const osbx_document_t& source) -> std::shared_ptr<const osbx_document_t> {
    for (auto scope = current_scope; scope; scope = scope->previous_)
        for (const auto& scene : scope->scenes_)
            if (scene.complete && scene.scene && scene.source == source) return scene.scene;
    return {};
}

auto osbx_lua_prepared_scope_t::rebase(const osbx_document_t& committed, const osbx_document_t& candidate,
    document_stamp_t capture, storyboard_id_t storyboard) -> result_t<osbx_lua_prepared_scene_t, failure_code_t> {
    if (!is_retained_source(committed, candidate)) return std::unexpected(failure_code_t::stale_generation);
    for (auto scope = current_scope; scope; scope = scope->previous_) {
        for (const auto& prepared : scope->scenes_) {
            if (prepared.stamp != capture || prepared.storyboard != storyboard || prepared.source != committed) continue;
            if (!prepared.complete || !prepared.scene) return std::unexpected(failure_code_t::unavailable);
            auto result = build_scene({.result = {.stamp = capture, .storyboard = storyboard, .source = candidate,
                .diagnostics = prepared.diagnostics, .complete = true}, .scene = candidate,
                .previous = &prepared, .retained_only = true});
            if (!result.complete || !result.scene) return std::unexpected(failure_code_t::unavailable);
            return result;
        }
    }
    return std::unexpected(failure_code_t::unavailable);
}

auto osbx_lua_prepared_scope_t::prepared(const osbx_document_t& source, document_stamp_t capture,
    storyboard_id_t storyboard, std::uint64_t captured_generation) -> const osbx_lua_prepared_scene_t* {
    if (!current_scope || current_scope->generation_ != captured_generation) return nullptr;
    for (auto scope = current_scope; scope; scope = scope->previous_) {
        if (scope->generation_ != captured_generation) continue;
        for (const auto& scene : scope->scenes_)
            if (scene.stamp == capture && scene.storyboard == storyboard && scene.source == source) return &scene;
    }
    return nullptr;
}

auto osbx_lua_prepared_scope_t::generation() -> std::uint64_t { return current_scope ? current_scope->generation_ : 0; }
auto osbx_lua_prepared_scope_t::diagnostic() -> std::string_view {
    if (current_scope)
        for (const auto& scene : current_scope->scenes_)
            if (!scene.diagnostics.empty()) return scene.diagnostics.front().message;
    return {};
}
auto osbx_lua_prepared_scope_t::failure(const osbx_document_t& source) -> osbx_link_diagnostics_t {
    if (current_scope)
        for (const auto& scene : current_scope->scenes_)
            if (scene.source == source && !scene.diagnostics.empty()) return scene.diagnostics;
    osbx_link_diagnostics_t result;
    for (const auto& node : source.nodes) {
        const auto* link = node_link(node);
        if (!link) continue;
        const auto file = link->source.source_reference ? text_util::path_utf8(*link->source.source_reference) : "Lua link";
        result.push_back({.code = osbx_diagnostic_code_t::unprepared_lua_link,
            .message = file + ":1: Lua link has no successful prepared result",
            .location = {.id = node.id, .provenance = {.file = file, .line = 1}}});
    }
    return result;
}

auto prepare_osbx_lua_scenes(const song_document_t& document, const osbx_lua_backend_t& backend,
    std::span<const osbx_lua_prepared_scene_t> previous) -> std::vector<osbx_lua_prepared_scene_t> {
    std::vector<osbx_lua_prepared_scene_t> result;
    for (const auto& storyboard : document.storyboards) {
        if (!storyboard.osbx || !has_osbx_lua_links(*storyboard.osbx)) continue;
        const auto found = std::ranges::find(previous, storyboard.id, &osbx_lua_prepared_scene_t::storyboard);
        if (found != previous.end() && found->stamp.is_current(document) && found->source == *storyboard.osbx && found->complete) {
            result.push_back(*found);
            continue;
        }
        auto prepared = osbx_lua_prepared_scene_t::from(*storyboard.osbx,
            document_stamp_t::from(document), backend, found == previous.end() ? nullptr : &*found);
        prepared.storyboard = storyboard.id;
        result.push_back(std::move(prepared));
    }
    return result;
}

auto osbx_materialize_link_t::apply(document_command_transaction_t& transaction) const
    -> attempt_t<document_edit_effect_t> {
    const auto& current = transaction.current_document();
    const auto found = std::ranges::find(current.storyboards, storyboard, &storyboard_document_t::id);
    if (found == current.storyboards.end() || !found->osbx ||
        found->owning_generator_recipe || *found->osbx != prepared.source || !prepared.complete || !prepared.scene)
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Lua link preparation is missing, stale or read-only")}};
    const auto link = std::ranges::find_if(prepared.links, [&](const auto& value) {
        return value.node_id == node_id && value.target == target;
    });
    if (link == prepared.links.end())
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Lua link no longer exists")}};
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        auto& scene = *std::ranges::find(song.storyboards, storyboard, &storyboard_document_t::id)->osbx;
        auto& node = *std::ranges::find(scene.nodes, node_id, &osbx_node_t::id);
        if (target == "children") {
            const auto layer = node.layer;
            node.children_link.reset();
            if (!append_children(scene, *link, layer))
                return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Materialized clip children exceed aggregate limits")}};
        } else {
            auto* track = target_track(node, target);
            if (!track) return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Lua track no longer exists")}};
            if (auto failure = materialize_track(*track, *link); !failure.empty())
                return {.diagnostics = {error(diagnostic_code_t::invalid_binding, std::move(failure))}};
        }
        if (!validate_osbx_document(scene).value)
            return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Materialized Lua link violates scene limits")}};
        return {.value = document_edit_effect_t{.changed = true,
            .command_label = target == "children" ? "Detach Lua children" : "Bake Lua track to keys"}};
    });
}
auto apply_osbx_materialize_link(document_history_t& history, const osbx_materialize_link_t& edit)
    -> document_command_result_t {
    document_command_transaction_t transaction{history, edit.prepared.stamp, "Lua link preparation",
        document_identity_policy_t::project_and_revision, diagnostic_code_t::invalid_binding};
    return transaction.finish_execution(edit.apply(transaction));
}
} // namespace wormedit

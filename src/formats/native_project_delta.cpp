#include <wormedit/formats/native_project_delta.h>
#include <wormedit/core/bms_program_edit.h>
#include "native_project_value_codec.h"
#include "../core/bms_native_content_internal.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {
using json = nlohmann::json;
using namespace native_project_internal;
constexpr std::array<std::string_view, 5> collection_keys{"id", "difficulty", "stable_id", "value/id", "chart"};

auto collection_identity(const json& value, std::string_view key) -> const json& {
    return value.at(json::json_pointer{"/" + std::string{key}});
}

struct value_delta_t {
    json patch = json::object();
    std::size_t removed_bytes{};
    auto operator==(const value_delta_t&) const -> bool = default;
};

auto identity(const json& value) -> std::uint64_t {
    if (!value.is_number_unsigned() || !value.get<std::uint64_t>())
        throw std::invalid_argument("Delta identity must be a nonzero unsigned integer.");
    return value.get<std::uint64_t>();
}
auto document_number(const json& value) -> std::uint64_t {
    if (!value.is_number_unsigned()) throw std::invalid_argument("Native document identity must be unsigned.");
    return value.get<std::uint64_t>();
}
auto array_key(const json& before, const json& after) -> std::string_view {
    for (const auto key : collection_keys) {
        const auto has_key = [key](const json& entry) {
            return entry.is_object() && entry.contains(json::json_pointer{"/" + std::string{key}});
        };
        if ((!before.empty() || !after.empty()) && std::ranges::all_of(before, has_key) && std::ranges::all_of(after, has_key))
            return key;
    }
    return {};
}
auto build_dom_delta(const json& before, const json& after) -> value_delta_t;
auto build_value_delta(const chart_document_t&, const chart_document_t&) -> value_delta_t;
auto build_value_delta(const measure_chunk_t&, const measure_chunk_t&) -> value_delta_t;
auto build_value_delta(const bms_program_node_t&, const bms_program_node_t&) -> value_delta_t;

// JSON numeric equality merges signed zero. Native saves retain its spelling,
// including inside otherwise equal arrays/objects, so compare encoded leaves.
auto is_encoded_equal(const json& before, const json& after) -> bool {
    if (before.type() != after.type() || before.size() != after.size()) return false;
    if (before.is_object()) {
        for (const auto& [key, value] : before.items())
            if (!after.contains(key) || !is_encoded_equal(value, after.at(key))) return false;
        return true;
    }
    if (before.is_array()) {
        for (std::size_t i = 0; i < before.size(); ++i)
            if (!is_encoded_equal(before[i], after[i])) return false;
        return true;
    }
    if (before.is_number_float()) return before.dump() == after.dump();
    return before == after;
}

auto build_object_delta(const json& before, const json& after) -> value_delta_t {
    value_delta_t result;
    json fields = json::object(), removed = json::array();
    for (const auto& [key, value] : before.items()) {
        if (after.contains(key)) continue;
        removed.push_back(key);
        result.removed_bytes += value.dump().size() + json(key).dump().size() + 2;
    }
    for (const auto& [key, value] : after.items()) {
        auto change = before.contains(key) ? build_dom_delta(before.at(key), value)
            : value_delta_t{.patch = {{"set", value}}};
        if (change.patch.empty()) continue;
        fields[key] = std::move(change.patch);
        result.removed_bytes += change.removed_bytes;
    }
    if (!fields.empty()) result.patch["fields"] = std::move(fields);
    if (!removed.empty()) result.patch["erase"] = std::move(removed);
    return result;
}

template<class Value> auto build_value_delta(const Value& before, const Value& after) -> value_delta_t {
    if constexpr (requires { before == after; }) {
        if (before == after) return {};
    }
    return build_dom_delta(encode_native_value(before), encode_native_value(after));
}

// Both typed and encoded collections use the same ID/order algebra. Typed values
// skip equality before encoding; encoded scoped owners retain the native pool form.
class collection_delta_t {
  public:
    explicit collection_delta_t(std::string_view key) { result_.patch["key"] = key; }
    void insert(std::uint64_t id, json value) { result_.patch["upsert"].push_back({{"id", id}, {"value", std::move(value)}}); }
    void update(std::uint64_t id, value_delta_t change) {
        if (change.patch.empty()) return;
        result_.patch["update"].push_back({{"id", id}, {"patch", std::move(change.patch)}});
        result_.removed_bytes += change.removed_bytes;
    }
    void remove(std::uint64_t id, const json& value) {
        result_.patch["remove"].push_back(id);
        result_.removed_bytes += value.dump().size() + 1;
    }
    auto finish(const std::vector<std::uint64_t>& before, const std::vector<std::uint64_t>& after) -> value_delta_t {
        if (before != after) result_.patch["order"] = after;
        if (result_.patch.size() == 1) result_.patch.clear();
        return std::move(result_);
    }
  private:
    value_delta_t result_;
};

template<class Values> auto build_collection_delta(const Values& before, const Values& after) -> value_delta_t {
    using Value = std::ranges::range_value_t<Values>;
    std::unordered_map<std::uint64_t, const Value*> previous;
    std::unordered_set<std::uint64_t> seen;
    previous.reserve(before.size());
    seen.reserve(after.size());
    std::vector<std::uint64_t> old_order, new_order;
    old_order.reserve(before.size());
    new_order.reserve(after.size());
    collection_delta_t result{"id"};
    for (const auto& value : before) {
        const auto id = value.id.value;
        if (!id || !previous.emplace(id, &value).second) throw std::invalid_argument("Ambiguous delta base identity.");
        old_order.push_back(id);
    }
    for (const auto& value : after) {
        const auto id = value.id.value;
        if (!id || !seen.insert(id).second) throw std::invalid_argument("Ambiguous delta result identity.");
        new_order.push_back(id);
        const auto found = previous.find(id);
        if (found == previous.end()) result.insert(id, encode_native_value(value));
        else result.update(id, build_value_delta(*found->second, value));
    }
    for (const auto id : old_order)
        if (!seen.contains(id)) result.remove(id, encode_native_value(*previous.at(id)));
    return result.finish(old_order, new_order);
}

struct encoded_collection_t {
    std::map<std::uint64_t, json> values;
    std::vector<std::uint64_t> order;
    [[nodiscard]] static auto from(const json& array, std::string_view key) -> encoded_collection_t {
        if (!array.is_array()) throw std::invalid_argument("Delta collection must be an array.");
        encoded_collection_t result;
        for (const auto& value : array) {
            const auto id = identity(collection_identity(value, key));
            if (!result.values.emplace(id, value).second) throw std::invalid_argument("Duplicate delta collection identity.");
            result.order.push_back(id);
        }
        return result;
    }
};
auto build_encoded_collection_delta(const json& before, const json& after, std::string_view key) -> value_delta_t {
    const auto previous = encoded_collection_t::from(before, key);
    const auto next = encoded_collection_t::from(after, key);
    collection_delta_t result{key};
    for (const auto id : next.order) {
        const auto& value = next.values.at(id);
        if (!previous.values.contains(id)) result.insert(id, value);
        else result.update(id, build_dom_delta(previous.values.at(id), value));
    }
    for (const auto id : previous.order)
        if (!next.values.contains(id)) result.remove(id, previous.values.at(id));
    return result.finish(previous.order, next.order);
}
auto build_dom_delta(const json& before, const json& after) -> value_delta_t {
    if (is_encoded_equal(before, after)) return {};
    if (before.is_object() && after.is_object()) return build_object_delta(before, after);
    if (before.is_array() && after.is_array()) {
        const auto key = array_key(before, after);
        if (!key.empty()) return build_encoded_collection_delta(before, after, key);
    }
    return {.patch = {{"set", after}}, .removed_bytes = before.dump().size()};
}

template<class Owner> auto build_owner_delta(const Owner& before, const Owner& after) -> value_delta_t {
    auto result = build_object_delta(encode_native_fields(before), encode_native_fields(after));
    visit_native_collections(before, [&](std::string_view key, auto member, bool optional) {
        const auto& previous = before.*member;
        const auto& next = after.*member;
        auto change = build_collection_delta(previous, next);
        if (change.patch.empty()) return;
        if (optional && next.empty()) {
            result.patch["erase"].push_back(key);
            result.removed_bytes += json(key).dump().size() + 3;
        } else if (optional && previous.empty()) {
            json values = json::array();
            for (const auto& value : next) values.push_back(encode_native_value(value));
            result.patch["fields"][key] = {{"set", std::move(values)}};
        } else result.patch["fields"][key] = std::move(change.patch);
        result.removed_bytes += change.removed_bytes;
    });
    return result;
}
auto build_value_delta(const chart_document_t& before, const chart_document_t& after) -> value_delta_t {
    return build_owner_delta(before, after);
}
auto build_value_delta(const measure_chunk_t& before, const measure_chunk_t& after) -> value_delta_t {
    if (before == after) return {};
    return build_owner_delta(before, after);
}

auto build_value_delta(const bms_program_node_t& before, const bms_program_node_t& after) -> value_delta_t {
    if (before == after) return {};
    const auto* previous = std::get_if<bms_chart_content_t>(&before.value);
    const auto* next = std::get_if<bms_chart_content_t>(&after.value);
    if (!previous || !next) return build_dom_delta(encode_native_value(before), encode_native_value(after));
    auto result = build_object_delta(encode_native_fields(before), encode_native_fields(after));
    auto items = build_collection_delta(previous->items, next->items);
    if (!items.patch.empty()) {
        auto* field = &result.patch;
        for (const auto key : native_bms_content_fields) field = &(*field)["fields"][key];
        *field = std::move(items.patch);
        result.removed_bytes += items.removed_bytes;
    }
    return result;
}
auto build_catalog_dependencies(const bms_program_t& program) -> std::vector<std::pair<bool, std::uint64_t>> {
    std::vector<std::pair<bool, std::uint64_t>> result;
    std::set<std::pair<bool, std::uint64_t>> seen;
    const auto include = [&](bool is_asset, std::uint64_t id) {
        if (id && seen.emplace(is_asset, id).second) result.emplace_back(is_asset, id);
    };
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (const auto& item : chart->items) {
            if (const auto* declaration = std::get_if<bms_resource_declaration_t>(&item.value))
                include(false, declaration->resource.value);
            const auto* native = std::get_if<bms_native_content_t>(&item.value);
            if (!native) continue;
            if (const auto resource = event_resource(native->payload())) include(false, resource->value);
            if (const auto* channel = std::get_if<sound_channel_t>(&native->payload()))
                include(true, channel->source_clip.source.value);
        }
    }
    return result;
}
auto has_same_catalog_inputs(const bms_prepared_program_t& before, const bms_prepared_program_t& after) -> bool {
    return before.resources == after.resources && before.audio_assets == after.audio_assets &&
        before.visual_assets == after.visual_assets && before.sound_layer_defaults == after.sound_layer_defaults &&
        before.settings.overrides == after.settings.overrides &&
        build_catalog_dependencies(before.program) == build_catalog_dependencies(after.program);
}
auto build_scoped_owner_delta(const bms_program_document_t& before_owner, const song_document_t& before,
    const bms_program_document_t& after_owner, const song_document_t& after) -> value_delta_t {
    // Equal catalog inputs have equal canonical pools even when the immutable
    // owner's local cache is stale. Omit those equal fields without copying the graph.
    const bool same_catalog = before.resources == after.resources && before.audio_assets == after.audio_assets &&
        before.visual_assets == after.visual_assets &&
        has_same_catalog_inputs(before_owner.prepared(), after_owner.prepared());
    auto result = same_catalog ? build_object_delta(encode_native_fields(before_owner), encode_native_fields(after_owner))
        : build_object_delta(encode_native_program_fields(before_owner, before), encode_native_program_fields(after_owner, after));
    auto nodes = build_collection_delta(before_owner.prepared().program.nodes(), after_owner.prepared().program.nodes());
    if (!nodes.patch.empty()) result.patch["fields"]["nodes"] = std::move(nodes.patch);
    result.removed_bytes += nodes.removed_bytes;
    return result;
}

auto build_programs(const song_document_t& song) -> std::map<std::uint64_t, const bms_program_document_t*> {
    std::map<std::uint64_t, const bms_program_document_t*> result;
    for (const auto& owner : song.prepared_bms_programs) {
        if (!owner || !owner->difficulty() || !result.emplace(owner->difficulty().value, owner.get()).second)
            throw std::invalid_argument("Ambiguous scoped BMS owner.");
    }
    return result;
}
auto build_program_delta(const song_document_t& before, const song_document_t& after) -> value_delta_t {
    const auto previous = build_programs(before), next = build_programs(after);
    const bool catalog_changed = before.resources != after.resources || before.audio_assets != after.audio_assets ||
        before.visual_assets != after.visual_assets;
    std::vector<std::uint64_t> old_order, new_order;
    collection_delta_t result{"difficulty"};
    for (const auto& owner : before.prepared_bms_programs) old_order.push_back(owner->difficulty().value);
    for (const auto& owner : after.prepared_bms_programs) {
        const auto id = owner->difficulty().value;
        new_order.push_back(id);
        if (!previous.contains(id)) result.insert(id, encode_native_program(*owner, after));
        else if (previous.at(id) != owner.get() || catalog_changed)
            result.update(id, build_scoped_owner_delta(*previous.at(id), before, *owner, after));
    }
    for (const auto id : old_order)
        if (!next.contains(id)) result.remove(id, encode_native_program(*previous.at(id), before));
    return result.finish(old_order, new_order);
}

// Only codec output enters the differ. The reader accepts the same native JSON
// values, validates patch structure, then lets the full native reader validate the graph.
void apply_patch_value(json& value, const json& patch);
void apply_object_patch(json& value, const json& patch) {
    if (!value.is_object() || patch.empty() || patch.size() > 2 ||
        (!patch.contains("fields") && !patch.contains("erase"))) throw std::invalid_argument("Invalid delta object patch.");
    for (const auto& [key, ignored] : patch.items()) {
        (void)ignored;
        if (key != "fields" && key != "erase") throw std::invalid_argument("Unknown delta object operation.");
    }
    if (patch.contains("erase")) {
        if (!patch.at("erase").is_array()) throw std::invalid_argument("Invalid delta field removals.");
        for (const auto& key : patch.at("erase"))
            if (value.erase(key.get<std::string>()) != 1) throw std::invalid_argument("Missing removed delta field.");
    }
    if (!patch.contains("fields")) return;
    if (!patch.at("fields").is_object()) throw std::invalid_argument("Invalid delta fields.");
    for (const auto& [key, change] : patch.at("fields").items()) {
        if (!value.contains(key) && !change.contains("set")) throw std::invalid_argument("Missing updated delta field.");
        apply_patch_value(value[key], change);
    }
}
void apply_collection_entry(encoded_collection_t& values, std::string_view operation,
    const json& entry, std::string_view key) {
    const auto id = identity(operation == "remove" ? entry : entry.at("id"));
    if (operation == "remove") {
        if (!values.values.erase(id)) throw std::invalid_argument("Missing removed delta identity.");
    } else if (operation == "upsert") {
        if (entry.size() != 2 || identity(collection_identity(entry.at("value"), key)) != id || values.values.contains(id))
            throw std::invalid_argument("Invalid delta insertion.");
        values.values.emplace(id, entry.at("value"));
    } else {
        if (entry.size() != 2 || !values.values.contains(id)) throw std::invalid_argument("Missing updated delta identity.");
        apply_patch_value(values.values.at(id), entry.at("patch"));
        if (identity(collection_identity(values.values.at(id), key)) != id) throw std::invalid_argument("Delta changed collection identity.");
    }
}
void apply_collection_changes(encoded_collection_t& values, const json& patch, std::string_view key) {
    std::set<std::uint64_t> touched;
    for (const auto* operation : {"remove", "upsert", "update"}) {
        if (!patch.contains(operation)) continue;
        if (!patch.at(operation).is_array()) throw std::invalid_argument("Invalid delta collection operations.");
        for (const auto& entry : patch.at(operation)) {
            const auto id = identity(operation == std::string_view{"remove"} ? entry : entry.at("id"));
            if (!touched.insert(id).second) throw std::invalid_argument("Repeated delta operation identity.");
            apply_collection_entry(values, operation, entry, key);
        }
    }
}

void apply_collection_patch(json& value, const json& patch) {
    const auto key = patch.at("key").get<std::string>();
    if (std::ranges::find(collection_keys, key) == collection_keys.end()) throw std::invalid_argument("Unknown delta collection key.");
    for (const auto& [operation, ignored] : patch.items()) {
        (void)ignored;
        if (operation != "key" && operation != "upsert" && operation != "update" && operation != "remove" && operation != "order")
            throw std::invalid_argument("Unknown delta collection operation.");
    }
    auto collection = encoded_collection_t::from(value, key);
    apply_collection_changes(collection, patch, key);
    if (patch.contains("order")) {
        if (!patch.at("order").is_array()) throw std::invalid_argument("Invalid delta collection order.");
        collection.order.clear();
        for (const auto& id : patch.at("order")) collection.order.push_back(identity(id));
    }
    std::set<std::uint64_t> seen;
    json result = json::array();
    for (const auto id : collection.order) {
        if (!seen.insert(id).second || !collection.values.contains(id)) throw std::invalid_argument("Invalid delta collection order.");
        result.push_back(std::move(collection.values.at(id)));
    }
    if (seen.size() != collection.values.size()) throw std::invalid_argument("Incomplete delta collection order.");
    value = std::move(result);
}
void apply_patch_value(json& value, const json& patch) {
    if (!patch.is_object() || patch.empty()) throw std::invalid_argument("Invalid delta patch.");
    if (patch.contains("set")) {
        if (patch.size() != 1) throw std::invalid_argument("Invalid delta replacement.");
        value = patch.at("set");
    } else if (patch.contains("key")) apply_collection_patch(value, patch);
    else apply_object_patch(value, patch);
}

// Cache collection positions by JSON path, not element address: array edits can
// move elements. Replacements invalidate only their subtree's derived indices.
class encoded_patch_t {
  public:
    encoded_patch_t(json& root, native_project_limits_t limits) : root_(root), limits_(limits) {
        bytes_ = root.dump().size();
        nodes_ = count_nodes(root);
    }
    void apply(const json& change) {
        const auto old_bytes = bytes_, old_nodes = nodes_;
        const auto project = document_number(root_.at("document").at("id"));
        try {
            if (!change.empty()) patch(root_.at("document"), change, "/document");
            if (document_number(root_.at("document").at("id")) != project)
                throw std::invalid_argument("Delta changed project identity.");
            (void)document_number(root_.at("document").at("revision"));
            undo_.clear();
        } catch (...) {
            for (auto item = undo_.rbegin(); item != undo_.rend(); ++item) {
                const json::json_pointer path{item->path};
                if (item->previous) root_[path] = std::move(*item->previous);
                else root_.at(path.parent_pointer()).erase(path.back());
            }
            undo_.clear();
            indices_.clear();
            bytes_ = old_bytes;
            nodes_ = old_nodes;
            throw;
        }
    }
  private:
    struct undo_t { std::string path; std::optional<json> previous; };
    json& root_;
    native_project_limits_t limits_;
    std::size_t bytes_{}, nodes_{};
    std::vector<undo_t> undo_;
    std::unordered_map<std::string, std::unordered_map<std::uint64_t, std::size_t>> indices_;
    static auto count_nodes(const json& value) -> std::size_t {
        std::size_t count{1};
        if (value.is_structured())
            for (const auto& item : value) count += count_nodes(item);
        return count;
    }
    static auto child_path(const std::string& path, const std::string& key) -> std::string {
        return (json::json_pointer{path} / key).to_string();
    }
    void invalidate(const std::string& path) {
        std::erase_if(indices_, [&](const auto& entry) {
            return entry.first == path || entry.first.starts_with(path + "/");
        });
    }
    void check_budget() const {
        if (bytes_ > limits_.maximum_source_bytes || nodes_ > limits_.maximum_json_nodes)
            throw std::length_error("Reconstructed native tree exceeds its codec budget.");
    }
    void check_depth(const json& value, std::size_t depth) const {
        if (!value.is_structured()) return;
        if (depth > limits_.maximum_nesting_depth) throw std::length_error("Native tree nesting budget exceeded.");
        for (const auto& item : value) check_depth(item, depth + 1);
    }
    void replace(json& value, json next, const std::string& path) {
        check_depth(next, static_cast<std::size_t>(std::ranges::count(path, '/')) + 1);
        bytes_ = bytes_ - value.dump().size() + next.dump().size();
        nodes_ = nodes_ - count_nodes(value) + count_nodes(next);
        check_budget();
        undo_.push_back({.path = path, .previous = std::move(value)});
        value = std::move(next);
        invalidate(path);
    }
    void erase(json& value, const std::string& key, const std::string& path) {
        if (!value.contains(key)) throw std::invalid_argument("Missing removed delta field.");
        bytes_ -= value.at(key).dump().size() + json(key).dump().size() + 1 + (value.size() > 1 ? 1 : 0);
        nodes_ -= count_nodes(value.at(key));
        const auto child = child_path(path, key);
        undo_.push_back({.path = child, .previous = std::move(value.at(key))});
        value.erase(key);
        invalidate(child);
    }
    void patch_object(json& value, const json& change, const std::string& path) {
        if (!value.is_object() || change.size() > 2 || (!change.contains("fields") && !change.contains("erase")))
            throw std::invalid_argument("Invalid delta object patch.");
        for (const auto& [key, ignored] : change.items()) {
            (void)ignored;
            if (key != "fields" && key != "erase") throw std::invalid_argument("Unknown delta object operation.");
        }
        if (change.contains("erase")) {
            if (!change.at("erase").is_array()) throw std::invalid_argument("Invalid delta field removals.");
            for (const auto& key : change.at("erase")) erase(value, key.get<std::string>(), path);
        }
        if (change.contains("fields")) patch_fields(value, change.at("fields"), path);
    }
    void patch_fields(json& value, const json& fields, const std::string& path) {
        if (!fields.is_object()) throw std::invalid_argument("Invalid delta fields.");
        for (const auto& [key, change] : fields.items()) {
            const auto child = child_path(path, key);
            if (!value.contains(key)) {
                if (!change.is_object() || change.size() != 1 || !change.contains("set"))
                    throw std::invalid_argument("Missing updated delta field.");
                check_depth(change.at("set"), static_cast<std::size_t>(std::ranges::count(child, '/')) + 1);
                bytes_ += json(key).dump().size() + 1 + (value.empty() ? 0 : 1) + change.at("set").dump().size();
                nodes_ += count_nodes(change.at("set"));
                check_budget();
                undo_.push_back({.path = child});
                value[key] = change.at("set");
            } else patch(value.at(key), change, child);
        }
    }
    auto index(const json& value, const std::string& path, std::string_view key)
        -> const std::unordered_map<std::uint64_t, std::size_t>& {
        const auto found = indices_.find(path);
        if (found != indices_.end()) return found->second;
        if (!value.is_array()) throw std::invalid_argument("Delta collection must be an array.");
        std::unordered_map<std::uint64_t, std::size_t> positions;
        for (std::size_t i = 0; i < value.size(); ++i)
            if (!positions.emplace(identity(collection_identity(value[i], key)), i).second)
                throw std::invalid_argument("Duplicate delta collection identity.");
        return indices_.emplace(path, std::move(positions)).first->second;
    }
    void patch_updates(json& value, const json& change, const std::string& path) {
        const auto key = change.at("key").get<std::string>();
        if (std::ranges::find(collection_keys, key) == collection_keys.end())
            throw std::invalid_argument("Unknown delta collection key.");
        if (change.size() != 2 || !change.at("update").is_array())
            throw std::invalid_argument("Invalid delta collection operations.");
        std::set<std::uint64_t> seen;
        // A nested replacement may invalidate cached descendant indices. Copy the
        // small set of touched positions before mutating the tree/cache.
        std::vector<std::pair<std::size_t, const json*>> updates;
        const auto& positions = index(value, path + "/" + key, key);
        for (const auto& entry : change.at("update")) {
            const auto id = identity(entry.at("id"));
            if (entry.size() != 2 || !seen.insert(id).second || !positions.contains(id))
                throw std::invalid_argument("Invalid updated delta identity.");
            updates.emplace_back(positions.at(id), &entry);
        }
        for (const auto& [position, entry] : updates) {
            patch(value[position], entry->at("patch"), child_path(path, std::to_string(position)));
            if (identity(collection_identity(value[position], key)) != identity(entry->at("id")))
                throw std::invalid_argument("Delta changed collection identity.");
        }
    }
    void patch(json& value, const json& change, const std::string& path) {
        if (!change.is_object() || change.empty()) throw std::invalid_argument("Invalid delta patch.");
        if (change.contains("set")) {
            if (change.size() != 1) throw std::invalid_argument("Invalid delta replacement.");
            replace(value, change.at("set"), path);
        } else if (!change.contains("key")) patch_object(value, change, path);
        else if (change.contains("update") && change.size() == 2) patch_updates(value, change, path);
        else {
            // Inserts/removals/reorders are uncommon and can invalidate descendants.
            // The shared collection algebra validates them on detached storage.
            auto next = value;
            apply_collection_patch(next, change);
            replace(value, std::move(next), path);
        }
    }
};

auto parse_delta_json(std::string_view bytes, native_project_limits_t limits) -> json {
    if (bytes.size() > limits.maximum_source_bytes) throw std::length_error("Delta exceeds its byte budget.");
    std::size_t nodes{};
    std::vector<std::set<std::string>> keys(limits.maximum_nesting_depth + 1);
    return json::parse(bytes, [&](int depth, json::parse_event_t event, json& value) {
        if (depth < 0 || static_cast<std::size_t>(depth) >= limits.maximum_nesting_depth || ++nodes > limits.maximum_json_nodes)
            throw std::length_error("Delta exceeds its structural budget.");
        auto& seen = keys[static_cast<std::size_t>(depth)];
        if (event == json::parse_event_t::object_start) keys[static_cast<std::size_t>(depth) + 1].clear();
        if (event == json::parse_event_t::key && !seen.insert(value.get<std::string>()).second)
            throw std::invalid_argument("Duplicate delta key.");
        return true;
    });
}
} // namespace

auto native_project_delta_t::from(const song_document_t& before, const song_document_t& after)
    -> result_t<native_project_delta_t, error_t<failure_code_t>> {
    try {
        if (before.schema_version != current_document_schema_version || after.schema_version != current_document_schema_version || before.id != after.id)
            throw std::invalid_argument("Delta requires one current-schema project.");
        const auto legacy = [](const chart_document_t& chart) { return chart.source_family == format_family_t::bms; };
        if (std::ranges::any_of(before.charts, legacy) || std::ranges::any_of(after.charts, legacy))
            throw std::invalid_argument("Legacy BMS ownership requires a seed.");
        // No whole-document validation here (owner, 2026-10-27): it dominated the
        // append cost. Seeds validate on write and replay validates its final state.
        auto change = build_owner_delta(before, after);
        auto programs = build_program_delta(before, after);
        if (!programs.patch.empty()) {
            if (after.prepared_bms_programs.empty()) change.patch["erase"].push_back("prepared_bms_programs");
            else if (before.prepared_bms_programs.empty()) {
                json owners = json::array();
                for (const auto& owner : after.prepared_bms_programs) owners.push_back(encode_native_program(*owner, after));
                change.patch["fields"]["prepared_bms_programs"] = {{"set", std::move(owners)}};
            } else change.patch["fields"]["prepared_bms_programs"] = std::move(programs.patch);
            change.removed_bytes += programs.removed_bytes + 32;
        }
        native_project_delta_t result;
        result.is_empty_ = change.patch.empty();
        result.bytes_ = json{{"schema_version", current_document_schema_version}, {"document", std::move(change.patch)}}.dump();
        (void)parse_delta_json(result.bytes_, {});
        result.removed_bytes_ = change.removed_bytes;
        return result;
    } catch (const std::exception& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, exception.what()});
    }
}
auto native_project_delta_t::parse(std::string_view bytes, native_project_limits_t limits)
    -> result_t<native_project_delta_t, error_t<failure_code_t>> {
    try {
        const auto root = parse_delta_json(bytes, limits);
        if (!root.is_object() || root.size() != 2 || !root.at("schema_version").is_number_unsigned() ||
            root.at("schema_version") != current_document_schema_version || !root.at("document").is_object())
            throw std::invalid_argument("Unsupported native delta envelope.");
        native_project_delta_t result;
        result.bytes_ = bytes;
        result.is_empty_ = root.at("document").empty();
        return result;
    } catch (const std::length_error& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, exception.what()});
    } catch (const std::exception& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, exception.what()});
    }
}
auto native_project_delta_t::apply_on(const song_document_t& document, native_project_limits_t limits) const -> native_project_parse_outcome_t {
    try {
        auto encoded = encode_native_root(encode_native_value(document));
        const auto patch = parse_delta_json(bytes_, limits);
        encoded_patch_t replay{encoded, limits};
        replay.apply(patch.at("document"));
        return parse_native_project(encoded.dump(), limits);
    } catch (const std::exception& exception) {
        return {.diagnostics = {{diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
            exception.what(), "$"}}};
    }
}
struct native_project_replay_t::storage_t {
    json root;
    native_project_limits_t limits;
    encoded_patch_t patch;
    storage_t(json value, native_project_limits_t budgets)
        : root(std::move(value)), limits(budgets), patch(root, limits) {}
};
native_project_replay_t::native_project_replay_t(std::unique_ptr<storage_t> storage) : storage_(std::move(storage)) {}
native_project_replay_t::native_project_replay_t(native_project_replay_t&&) noexcept = default;
auto native_project_replay_t::operator=(native_project_replay_t&&) noexcept -> native_project_replay_t& = default;
native_project_replay_t::~native_project_replay_t() = default;
auto native_project_replay_t::from(std::string_view seed, native_project_limits_t limits)
    -> result_t<native_project_replay_t, error_t<failure_code_t>> {
    try {
        auto parsed = parse_native_root(seed, limits);
        if (!parsed) return std::unexpected(parsed.error());
        auto root = std::move(*parsed);
        if (!root.is_object() || root.size() != 3 || !root.at("document").is_object())
            throw std::invalid_argument("Invalid native seed envelope.");
        const auto& document = root.at("document");
        (void)document_number(document.at("id"));
        (void)document_number(document.at("revision"));
        (void)document_number(document.at("schema_version"));
        return native_project_replay_t{std::make_unique<storage_t>(std::move(root), limits)};
    } catch (const std::length_error& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, exception.what()});
    } catch (const std::exception& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, exception.what()});
    }
}
auto native_project_replay_t::apply(const native_project_delta_t& delta) -> result_t<void, error_t<failure_code_t>> {
    try {
        if (!can_apply_delta())
            throw std::invalid_argument("Delta requires a current-schema seed.");
        const auto change = parse_delta_json(delta.bytes(), storage_->limits);
        storage_->patch.apply(change.at("document"));
        return {};
    } catch (const std::length_error& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, exception.what()});
    } catch (const std::exception& exception) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, exception.what()});
    }
}
auto native_project_replay_t::finish() const -> native_project_parse_outcome_t {
    return decode_native_root(storage_->root, storage_->limits);
}
auto native_project_replay_t::can_apply_delta() const -> bool {
    const auto& schema = storage_->root.at("document").at("schema_version");
    return schema.is_number_unsigned() && schema == current_document_schema_version;
}
auto native_project_replay_t::project() const -> song_id_t {
    return song_id_t{document_number(storage_->root.at("document").at("id"))};
}
auto native_project_replay_t::revision() const -> std::uint64_t {
    return document_number(storage_->root.at("document").at("revision"));
}

auto native_project_delta_t::minimum_result_bytes(std::size_t base_bytes) const -> std::size_t {
    return base_bytes > removed_bytes_ ? base_bytes - removed_bytes_ : 0;
}
} // namespace wormedit

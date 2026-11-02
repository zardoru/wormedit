#include <wormedit/core/osb_to_osbx.h>
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/storyboard_preview.h>
#include "text_util.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numbers>
#include <tuple>

namespace wormedit {
namespace {
using diagnostics_t = std::vector<osb_to_osbx_diagnostic_t>;
enum class channel_t { scale, rotation, x, y, opacity, color };

auto is_formatting(std::string_view line) -> bool {
    const auto text = text_util::trim(line, text_util::ascii_whitespace);
    return text.empty() || text == "[Events]" || text.starts_with("//");
}

auto source_location(const storyboard_sprite_t& sprite, std::uint64_t line)
    -> osbx_diagnostic_location_t {
    return {.id = "OSB sprite " + std::to_string(sprite.stable_id),
        .provenance = {.line = static_cast<std::size_t>(line)}};
}

auto validate_source(const storyboard_document_model_t& model) -> diagnostics_t {
    diagnostics_t diagnostics;
    for (const auto& sample : model.samples)
        diagnostics.push_back({.code = failure_code_t::unsupported,
            .message = "OSB Sample requires the OSBX-10 sample mapping",
            .location = {.id = "OSB sample " + std::to_string(sample.stable_id),
                .provenance = {.line = static_cast<std::size_t>(sample.source_order)}}});
    for (const auto& line : model.retained_top_level_lines) {
        if (!is_formatting(line.text))
            diagnostics.push_back({.code = failure_code_t::unsupported,
                .message = "Retained OSB directive or animation cannot be converted",
                .location = {.provenance = {.line = static_cast<std::size_t>(line.source_order)}}});
    }
    for (const auto& sprite : model.sprites) {
        if (sprite.animation) {
            diagnostics.push_back({.code = failure_code_t::unsupported,
                .message = "OSB Animation requires the OSBX-10 animation mapping",
                .location = source_location(sprite, sprite.source_order)});
            continue;
        }
        for (const auto& node : sprite.nodes) {
            if (node.kind == storyboard_node_kind_t::command) continue;
            if (node.kind == storyboard_node_kind_t::retained_line && is_formatting(node.retained_line)) continue;
            diagnostics.push_back({.code = failure_code_t::unsupported,
                .message = "Loops and retained OSB directives require an explicit supported mapping",
                .location = source_location(sprite, node.source_order)});
        }
    }
    return diagnostics;
}

auto has_channel(channel_t channel, storyboard_opcode_t opcode) -> bool {
    switch (channel) {
    case channel_t::scale: return opcode == storyboard_opcode_t::scale || opcode == storyboard_opcode_t::vector_scale;
    case channel_t::rotation: return opcode == storyboard_opcode_t::rotate;
    case channel_t::x: return opcode == storyboard_opcode_t::move || opcode == storyboard_opcode_t::move_x;
    case channel_t::y: return opcode == storyboard_opcode_t::move || opcode == storyboard_opcode_t::move_y;
    case channel_t::opacity: return opcode == storyboard_opcode_t::fade;
    case channel_t::color: return opcode == storyboard_opcode_t::color;
    }
    return false;
}

auto channel_value(channel_t channel, const storyboard_command_t& command, bool end) -> std::vector<double> {
    const auto& value = end ? command.to : command.from;
    switch (channel) {
    case channel_t::scale:
        return {value[0], command.opcode == storyboard_opcode_t::scale ? value[0] : value[1]};
    case channel_t::rotation: return {value[0] * (180 / std::numbers::pi)};
    case channel_t::x: return {value[0], 0};
    case channel_t::y: return {0, command.opcode == storyboard_opcode_t::move ? value[1] : value[0]};
    case channel_t::opacity: return {value[0]};
    case channel_t::color: return {value[0] / 255, value[1] / 255, value[2] / 255, 1};
    }
    return {};
}

struct source_command_t {
    const storyboard_node_t* node{};
    std::size_t visit{};
    [[nodiscard]] auto order() const {
        const auto& command = node->command;
        return std::tuple{command.start_time_ms,
            command.source_order ? command.source_order : node->source_order, visit};
    }
};

auto channel_commands(const storyboard_sprite_t& sprite, channel_t channel) -> std::vector<source_command_t> {
    std::vector<source_command_t> commands;
    for (std::size_t index = 0; index < sprite.nodes.size(); ++index) {
        const auto& node = sprite.nodes[index];
        if (node.kind == storyboard_node_kind_t::command && has_channel(channel, node.command.opcode))
            commands.push_back({.node = &node, .visit = index});
    }
    std::ranges::sort(commands, {}, &source_command_t::order);
    // At a shared start only the last source/visit wins, even after it ends.
    std::vector<source_command_t> selected;
    for (const auto& command : commands) {
        if (!selected.empty() && selected.back().node->command.start_time_ms == command.node->command.start_time_ms)
            selected.back() = command;
        else selected.push_back(command);
    }
    return selected;
}

class conversion_t {
  public:
    explicit conversion_t(const osbx_edit_ids_t& ids) : ids_(ids) {}

    auto run(const storyboard_document_model_t& model)
        -> attempt_t<osbx_document_t, diagnostics_t> {
        auto diagnostics = validate_source(model);
        if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
        // The canonical emitter validates every command, including ones hidden
        // by a later command at the same start; conversion cannot discard bad data.
        const auto emitted = emit_osb(model);
        for (const auto& diagnostic : emitted.diagnostics)
            diagnostics.push_back({.code = failure_code_t::invalid_data, .message = diagnostic.message,
                .location = {.provenance = {.line = diagnostic.line}}});
        if (!emitted.value) return {.diagnostics = std::move(diagnostics)};
        const auto preview = storyboard_preview_index_t::from(model);
        for (const auto& diagnostic : preview.diagnostics)
            diagnostics.push_back({.code = failure_code_t::invalid_data, .message = diagnostic.message,
                .location = {.provenance = {.line = diagnostic.line}}});
        if (!preview.value) return {.diagnostics = std::move(diagnostics)};
        preview_ = &*preview.value;
        std::vector<const storyboard_sprite_t*> sprites;
        for (const auto& sprite : model.sprites) sprites.push_back(&sprite);
        std::ranges::stable_sort(sprites, {}, &storyboard_sprite_t::source_order);
        for (const auto* sprite : sprites)
            if (!add_sprite(*sprite)) return {.diagnostics = std::move(diagnostics_)};
        const auto validation = validate_osbx_document(scene_);
        for (const auto& diagnostic : validation.diagnostics)
            diagnostics_.push_back({.code = failure_code_t::invalid_data,
                .message = diagnostic.message, .location = diagnostic.location});
        if (!diagnostics_.empty()) return {.diagnostics = std::move(diagnostics_)};
        return {.value = std::move(scene_)};
    }

  private:
    const osbx_edit_ids_t& ids_;
    osbx_document_t scene_;
    diagnostics_t diagnostics_;
    const storyboard_preview_index_t* preview_{}; // Borrowed only during run().
    std::int64_t sprite_begin_{};

    auto reject_source(const storyboard_sprite_t& sprite, std::uint64_t line, std::string message) -> bool {
        diagnostics_.push_back({.code = failure_code_t::unsupported, .message = std::move(message),
            .location = source_location(sprite, line)});
        return false;
    }

    template<class Value> void add_key(osbx_track_t<Value>& track, std::string_view owner,
        std::int64_t time, Value value, osbx_easing_t easing = osbx_step_easing_t{}) {
        const auto id = ids_.next(owner, "key", scene_);
        track.keys.push_back({.id = id, .time_ms = time, .value = std::move(value), .easing = std::move(easing)});
    }

    auto append_command(osbx_track_t<>& track, std::string_view owner, channel_t channel,
        const storyboard_command_t& command) -> bool {
        const bool instant = command.start_time_ms == command.end_time_ms;
        const auto from = channel_value(channel, command, instant);
        const auto to = channel_value(channel, command, true);
        const bool constant = from == to;
        if (!track.keys.empty() && track.keys.back().time_ms > command.start_time_ms) return false;
        if (!track.keys.empty() && track.keys.back().time_ms == command.start_time_ms) {
            if (track.keys.back().value != from && track.keys.size() > 1 &&
                !std::holds_alternative<osbx_step_easing_t>(track.keys[track.keys.size() - 2].easing)) return false;
            track.keys.pop_back();
        }
        add_key(track, owner, command.start_time_ms, from,
            constant ? osbx_easing_t{osbx_step_easing_t{}} : osbx_easing_t{osbx_osb_easing_t{.value = command.easing}});
        if (!constant) add_key(track, owner, command.end_time_ms, to);
        return true;
    }

    auto fill_track(osbx_track_t<>& track, std::string_view owner, channel_t channel,
        const storyboard_sprite_t& sprite) -> bool {
        const auto commands = channel_commands(sprite, channel);
        if (commands.empty()) return true;
        add_key(track, owner, sprite_begin_, track.default_value);
        for (const auto& source : commands) {
            const auto& command = source.node->command;
            if (!is_valid_easing(command.easing))
                return reject_source(sprite, source.node->source_order, "Invalid OSB easing");
            if (!append_command(track, owner, channel, command))
                return reject_source(sprite, source.node->source_order,
                    "Interrupted curves or discontinuous shared endpoints cannot be represented by unique OSBX keys");
        }
        return true;
    }

    auto add_transform(osbx_node_t& node, const storyboard_sprite_t& sprite,
        channel_t channel, osbx_transform_kind_t kind, std::vector<double> defaults) -> bool {
        node.ops.push_back({.id = ids_.next(node.id, "op", scene_), .kind = kind,
            .track = {.default_value = std::move(defaults)}});
        auto& op = node.ops.back();
        return fill_track(op.track, op.id, channel, sprite);
    }

    void set_lifetime(osbx_node_t& node, const storyboard_sprite_t& sprite) {
        const auto source = std::ranges::find(preview_->sources, sprite.stable_id, &storyboard_preview_source_t::stable_id);
        node.tracks.visibility.default_value = source != preview_->sources.end();
        if (source == preview_->sources.end()) return;
        node.lifetime = osbx_lifetime_t{.begin_ms = source->begin_time_ms, .end_ms = source->end_time_ms};
        sprite_begin_ = source->begin_time_ms;
    }

    auto add_sprite(const storyboard_sprite_t& sprite) -> bool {
        const auto layer = std::ranges::find(osbx_layer_names, sprite.layer);
        if (layer == osbx_layer_names.end() || !storyboard_origin_t::from(sprite.origin))
            return reject_source(sprite, sprite.source_order, "Unknown OSB layer or origin");
        if (!osbx_document_t::is_valid_project_path(sprite.path))
            return reject_source(sprite, sprite.source_order, "Image path is not a project-relative OSBX path");
        auto image = std::ranges::find(scene_.images, sprite.path, &osbx_image_t::file);
        if (image == scene_.images.end()) {
            scene_.images.push_back({.id = ids_.next("scene", "image", scene_), .file = sprite.path});
            image = std::prev(scene_.images.end());
        }
        scene_.nodes.push_back({.id = ids_.next("scene", "node", scene_), .kind = osbx_node_kind_t::sprite,
            .layer = static_cast<osbx_layer_t>(std::distance(osbx_layer_names.begin(), layer)),
            .order = static_cast<int>(scene_.nodes.size()), .image = image->id, .origin = sprite.origin,
            .provenance = {.line = static_cast<std::size_t>(sprite.source_order)}});
        auto& node = scene_.nodes.back();
        set_lifetime(node, sprite);
        if (!add_transform(node, sprite, channel_t::scale, osbx_transform_kind_t::scale, {1, 1}) ||
            !add_transform(node, sprite, channel_t::rotation, osbx_transform_kind_t::rotate, {0}) ||
            !add_transform(node, sprite, channel_t::x, osbx_transform_kind_t::translate, {sprite.x, 0}) ||
            !add_transform(node, sprite, channel_t::y, osbx_transform_kind_t::translate, {0, sprite.y})) return false;
        if (!fill_track(node.tracks.opacity, node.id, channel_t::opacity, sprite) ||
            !fill_track(node.tracks.color, node.id, channel_t::color, sprite)) return false;
        return true;
    }
};
} // namespace

auto convert_osb_to_osbx(const storyboard_document_model_t& model, const osbx_edit_ids_t& ids)
    -> attempt_t<osbx_document_t, std::vector<osb_to_osbx_diagnostic_t>> {
    return conversion_t{ids}.run(model);
}

} // namespace wormedit

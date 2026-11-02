#include <wormedit/core/storyboard_preview.h>
#include <wormedit/core/osbx_evaluator.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <functional>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace wormedit {

auto storyboard_draw_instance_t::is_valid() const -> bool {
    return stable_id != 0 && layer_order <= 4 && !layer.empty() &&
           !origin.empty() && !path.empty() && transform.is_finite() &&
           std::isfinite(logical_width) && logical_width > 0 &&
           std::isfinite(logical_height) && logical_height > 0 &&
           std::isfinite(opacity) && std::isfinite(red) &&
           std::isfinite(green) && std::isfinite(blue);
}

namespace {

auto layer_order(std::string_view layer) noexcept -> std::uint8_t {
    const auto found = std::ranges::find(osbx_layer_names, layer);
    return static_cast<std::uint8_t>(std::distance(osbx_layer_names.begin(), found));
}

auto is_evaluation_failed(const storyboard_evaluation_effect_t* result,
                       storyboard_frame_effect_t& frame) -> bool {
    if (result->succeeded)
        return false;
    frame.diagnostic = result->diagnostic;
    return true;
}

auto is_storyboard_instance_newer(const storyboard_evaluation_effect_t& candidate,
                const storyboard_evaluation_effect_t& current) noexcept -> bool {
    if (!candidate.value.present)
        return false;
    if (!current.value.present)
        return true;
    return std::tie(candidate.start_time_ms, candidate.source_order, candidate.visit_order) >=
           std::tie(current.start_time_ms, current.source_order, current.visit_order);
}

struct frame_evaluations_t {
    const storyboard_evaluation_effect_t& fade;
    const storyboard_evaluation_effect_t& move;
    const storyboard_evaluation_effect_t& move_x;
    const storyboard_evaluation_effect_t& move_y;
    const storyboard_evaluation_effect_t& scale;
    const storyboard_evaluation_effect_t& vector_scale;
    const storyboard_evaluation_effect_t& rotate;
    const storyboard_evaluation_effect_t& color;
};

auto apply_frame_evaluations(storyboard_draw_instance_t& instance,
                             const frame_evaluations_t& evaluations) -> void {
    const auto& [fade, move, move_x, move_y, scale, vector_scale, rotate, color] = evaluations;
    double x = instance.transform[4], y = instance.transform[5];
    double scale_x = 1, scale_y = 1, rotation = 0;
    if (move.value.present) {
        x = move.value.values[0];
        y = move.value.values[1];
    }
    if (is_storyboard_instance_newer(move_x, move)) {
        x = move_x.value.values[0];
    }
    if (is_storyboard_instance_newer(move_y, move)) {
        y = move_y.value.values[0];
    }
    if (scale.value.present) {
        scale_x = scale.value.values[0];
        scale_y = scale.value.values[0];
    }
    if (is_storyboard_instance_newer(vector_scale, scale)) {
        scale_x = vector_scale.value.values[0];
        scale_y = vector_scale.value.values[1];
    }
    if (fade.value.present) {
        instance.opacity = fade.value.values[0];
    }
    if (rotate.value.present) {
        rotation = rotate.value.values[0];
    }
    instance.transform = visual_affine_t::from(x, y, scale_x, scale_y, rotation);
    if (color.value.present) {
        instance.red = color.value.values[0];
        instance.green = color.value.values[1];
        instance.blue = color.value.values[2];
    }
}


auto apply_animation_path(const storyboard_sprite_t& sprite, const storyboard_preview_source_t& source,
    storyboard_time_t time, storyboard_frame_arena_t& arena, storyboard_draw_instance_t& instance,
    storyboard_frame_effect_t& result) -> bool {
    if (!sprite.animation) return true;
    const auto selected = sprite.animation->frame_index_at(time, source.begin_time_ms);
    if (!selected) {
        result.diagnostic = storyboard_diagnostic_t{storyboard_diagnostic_code_t::invalid_model, 0,
            "storyboard animation metadata is invalid"};
        return false;
    }
    const auto path = storyboard_animation_t::frame_path(sprite.path, *selected, arena);
    if (!path) { result.arena_error = path.error(); return false; }
    instance.path = *path;
    return true;
}

} // namespace

auto storyboard_draw_instance_t::from(const osbx_document_t& document, const osbx_node_t& node,
    const osbx_image_t& image) -> storyboard_draw_instance_t {
    return {.layer_order = static_cast<std::uint8_t>(node.layer),
        .layer = osbx_layer_names[static_cast<std::size_t>(node.layer)], .origin = node.origin,
        .path = image.file, .logical_width = document.canvas.width, .logical_height = document.canvas.height};
}

auto storyboard_draw_instance_t::with_evaluation(const osbx_draw_instance_t& evaluated) const
    -> storyboard_draw_instance_t {
    auto instance = *this;
    instance.transform = evaluated.world;
    instance.opacity = evaluated.opacity * evaluated.color[3];
    instance.red = evaluated.color[0] * 255;
    instance.green = evaluated.color[1] * 255;
    instance.blue = evaluated.color[2] * 255;
    instance.blend = evaluated.blend;
    return instance;
}

auto storyboard_draw_instance_t::with_evaluation(const osbx_draw_instance_t& evaluated,
    storyboard_frame_arena_t& arena) const -> result_t<storyboard_draw_instance_t, failure_code_t> {
    auto result = with_evaluation(evaluated);
    if (!evaluated.frame_index) return result;
    const auto selected = storyboard_animation_t::frame_path(path, *evaluated.frame_index, arena);
    if (!selected) return std::unexpected(selected.error());
    result.path = *selected;
    return result;
}

namespace {
void build_sample_sources(storyboard_preview_index_t& index, const storyboard_document_model_t& document) {
    index.sample_sources.reserve(document.samples.size());
    for (std::size_t i = 0; i < document.samples.size(); ++i)
        index.sample_sources.push_back({.sample_index = i, .captured = document.samples[i]});
    const auto identity = [](const auto& source) { return source.captured.stable_id; };
    std::ranges::sort(index.sample_sources, {}, identity);
    for (const auto& event : index.schedule.events) {
        if (event.edge != storyboard_event_t::edge_t::sample) continue;
        const auto found = std::ranges::lower_bound(index.sample_sources, event.stable_object_id, {}, identity);
        if (found != index.sample_sources.end()) found->event = event;
    }
}
} // namespace

auto storyboard_preview_index_t::sample_for(const storyboard_document_model_t& document,
    const storyboard_event_t& event, storyboard_conditional_layer_t choice) const -> const storyboard_sample_t* {
    if (event.edge != storyboard_event_t::edge_t::sample ||
        static_cast<unsigned>(choice) > static_cast<unsigned>(storyboard_conditional_layer_t::neither)) return nullptr;
    const auto found = std::ranges::lower_bound(sample_sources, event.stable_object_id, {},
        [](const auto& source) { return source.captured.stable_id; });
    if (found == sample_sources.end() || found->sample_index >= document.samples.size() || found->event != event)
        return nullptr;
    const auto& sample = document.samples[found->sample_index];
    if (sample != found->captured || !sample.is_valid() ||
        !is_storyboard_layer_visible(osbx_layer_names[static_cast<std::size_t>(sample.layer)], choice)) return nullptr;
    return &sample;
}

auto storyboard_preview_index_t::from(const storyboard_document_model_t& document,
                                    storyboard_schedule_options_t options)
    -> storyboard_preview_index_outcome_t {
    storyboard_preview_index_outcome_t result{};
    auto schedule = compile_storyboard_schedule(document, options);
    if (!schedule.value) {
        result.diagnostics = std::move(schedule.diagnostics);
        return result;
    }

    storyboard_preview_index_t candidate;
    try {
        candidate.schedule = std::move(*schedule.value);
        build_sample_sources(candidate, document);
        candidate.sources.resize(document.sprites.size());
        std::unordered_map<std::uint64_t, std::size_t> sprite_by_id;
        sprite_by_id.reserve(document.sprites.size());
        for (std::size_t index = 0; index < document.sprites.size(); ++index) {
            const auto& sprite = document.sprites[index];
            if (sprite.stable_id == 0 || !sprite_by_id.emplace(sprite.stable_id, index).second) {
                result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                    "storyboard preview requires unique nonzero sprite identifiers"});
                return result;
            }
            candidate.sources[index] = {
                .sprite_index = index,
                .stable_id = sprite.stable_id,
                .begin_time_ms = std::numeric_limits<std::int64_t>::max(),
                .end_time_ms = std::numeric_limits<std::int64_t>::min(),
                .layer_order = layer_order(sprite.layer),
                .source_order = sprite.source_order,
            };
        }
        for (const auto& event : candidate.schedule.events) {
            if (event.edge == storyboard_event_t::edge_t::sample) continue;
            const auto found = sprite_by_id.find(event.stable_object_id);
            if (found == sprite_by_id.end()) {
                result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                    "storyboard schedule refers to an unknown sprite"});
                return result;
            }
            auto& source = candidate.sources[found->second];
            source.begin_time_ms = std::min(source.begin_time_ms, event.time_ms);
            source.end_time_ms = std::max(source.end_time_ms, event.time_ms);
        }
        std::erase_if(candidate.sources, std::mem_fn(&storyboard_preview_source_t::has_sentinel_begin));
        std::ranges::sort(candidate.sources, {}, &storyboard_preview_source_t::sort_key);
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "storyboard preview index allocation failed"});
        return result;
    }
    result.value = std::move(candidate);
    return result;
}

auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
                               const storyboard_preview_index_t& index, std::int64_t time_ms,
                               storyboard_frame_arena_t& arena, storyboard_frame_options_t options)
    -> storyboard_frame_effect_t {
    return evaluate_storyboard_frame(document, index,
        storyboard_time_t{.milliseconds = time_ms}, arena, options);
}

auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
    const storyboard_preview_index_t& index, storyboard_time_t time,
    storyboard_frame_arena_t& arena, storyboard_frame_options_t options) -> storyboard_frame_effect_t {
    arena.reset();
    storyboard_frame_effect_t result{};
    if (!time.is_valid()) return {.arena_error = failure_code_t::invalid_request};
    const auto is_active = [&](const storyboard_preview_source_t& source) {
        return source.is_active(time.milliseconds) &&
            (time.milliseconds != source.end_time_ms || time.remainder == 0) &&
            (source.sprite_index >= document.sprites.size() ||
             is_storyboard_layer_visible(document.sprites[source.sprite_index].layer, options.conditional_layer));
    };
    const auto active_count = static_cast<std::size_t>(
        std::ranges::count_if(index.sources, is_active));
    if (active_count > options.maximum_instances) {
        result.diagnostic =
            storyboard_diagnostic_t{storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                    "storyboard frame exceeds its configured instance limit"};
        return result;
    }
    if (active_count == 0)
        return result;
    if (active_count >
        std::numeric_limits<std::size_t>::max() / sizeof(storyboard_draw_instance_t)) {
        result.diagnostic =
            storyboard_diagnostic_t{storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                    "storyboard frame instance storage overflowed"};
        return result;
    }
    const auto allocation = arena.allocate(active_count * sizeof(storyboard_draw_instance_t),
                                                 alignof(storyboard_draw_instance_t));
    if (!allocation) {
        result.arena_error = allocation.error();
        return result;
    }
    auto* output = static_cast<storyboard_draw_instance_t*>(*allocation);
    std::size_t output_count = 0;
    for (const auto& source : index.sources) {
        if (!is_active(source))
            continue;
        if (source.sprite_index >= document.sprites.size() ||
            document.sprites[source.sprite_index].stable_id != source.stable_id) {
            result.diagnostic =
                storyboard_diagnostic_t{storyboard_diagnostic_code_t::invalid_model, 0,
                                        "storyboard preview index is stale for its document"};
            return result;
        }
        const auto& sprite = document.sprites[source.sprite_index];
        using evaluator_t = storyboard_evaluation_effect_t (*)(const storyboard_sprite_t&,
            storyboard_opcode_t, storyboard_time_t, std::size_t);
        const auto evaluate = std::bind_front(std::bind_back(static_cast<evaluator_t>(&evaluate_storyboard_property),
            time, options.maximum_nesting), std::cref(sprite));
        const auto fade = evaluate(storyboard_opcode_t::fade);
        const auto move = evaluate(storyboard_opcode_t::move);
        const auto move_x = evaluate(storyboard_opcode_t::move_x);
        const auto move_y = evaluate(storyboard_opcode_t::move_y);
        const auto scale = evaluate(storyboard_opcode_t::scale);
        const auto vector_scale = evaluate(storyboard_opcode_t::vector_scale);
        const auto rotate = evaluate(storyboard_opcode_t::rotate);
        const auto color = evaluate(storyboard_opcode_t::color);
        const std::array evaluations{&fade,  &move,         &move_x, &move_y,
                                     &scale, &vector_scale, &rotate, &color};
        if (std::ranges::any_of(evaluations, std::bind_back(is_evaluation_failed, std::ref(result)))) {
            return result;
        }

        storyboard_draw_instance_t instance{
            .document_order = source.sprite_index,
            .stable_id = sprite.stable_id,
            .source_order = sprite.source_order,
            .layer_order = source.layer_order,
            .layer = sprite.layer,
            .origin = sprite.origin,
            .path = sprite.path,
            .transform = visual_affine_t::from(sprite.x, sprite.y),
        };
        if (!apply_animation_path(sprite, source, time, arena, instance, result)) return result;
        apply_frame_evaluations(instance,
                                {fade, move, move_x, move_y, scale, vector_scale, rotate, color});
        std::construct_at(output + output_count, instance);
        ++output_count;
    }
    result.instances = {output, output_count};
    return result;
}


} // namespace wormedit

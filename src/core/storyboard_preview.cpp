#include <wormedit/core/storyboard_preview.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

auto layer_order(std::string_view layer) noexcept -> std::uint8_t {
    if (layer == "Background")
        return 0;
    if (layer == "Fail")
        return 1;
    if (layer == "Pass")
        return 2;
    if (layer == "Foreground")
        return 3;
    return 4;
}

auto invalid_index(std::string message) -> storyboard_preview_index_result_t {
    storyboard_preview_index_result_t result;
    result.diagnostics.push_back(
        {storyboard_diagnostic_code_t::invalid_model, 0, std::move(message)});
    return result;
}

auto evaluation_failed(const storyboard_evaluation_result_t& result,
                       storyboard_frame_result_t& frame) -> bool {
    if (!result.failed)
        return false;
    frame.diagnostic = result.diagnostic;
    return true;
}

auto newer_than(const storyboard_evaluation_result_t& candidate,
                const storyboard_evaluation_result_t& current) noexcept -> bool {
    if (!candidate.value.present)
        return false;
    if (!current.value.present)
        return true;
    return std::tie(candidate.start_time_ms, candidate.source_order, candidate.visit_order) >=
           std::tie(current.start_time_ms, current.source_order, current.visit_order);
}

} // namespace

auto build_storyboard_preview_index(const storyboard_document_model_t& document,
                                    storyboard_schedule_options_t options)
    -> storyboard_preview_index_result_t {
    storyboard_preview_index_result_t result;
    auto schedule = compile_storyboard_schedule(document, options);
    if (!schedule) {
        result.diagnostics = std::move(schedule.diagnostics);
        return result;
    }

    storyboard_preview_index_t candidate;
    try {
        candidate.sources.resize(document.sprites.size());
        std::unordered_map<std::uint64_t, std::size_t> sprite_by_id;
        sprite_by_id.reserve(document.sprites.size());
        for (std::size_t index = 0; index < document.sprites.size(); ++index) {
            const auto& sprite = document.sprites[index];
            if (sprite.stable_id == 0 || !sprite_by_id.emplace(sprite.stable_id, index).second) {
                return invalid_index(
                    "storyboard preview requires unique nonzero sprite identifiers");
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
        for (const auto& event : schedule.schedule->events) {
            const auto found = sprite_by_id.find(event.stable_object_id);
            if (found == sprite_by_id.end()) {
                return invalid_index("storyboard schedule refers to an unknown sprite");
            }
            auto& source = candidate.sources[found->second];
            source.begin_time_ms = std::min(source.begin_time_ms, event.time_ms);
            source.end_time_ms = std::max(source.end_time_ms, event.time_ms);
        }
        std::erase_if(candidate.sources, [](const auto& source) {
            return source.begin_time_ms == std::numeric_limits<std::int64_t>::max();
        });
        std::ranges::sort(candidate.sources, [](const auto& left, const auto& right) {
            return std::tie(left.layer_order, left.source_order, left.sprite_index) <
                   std::tie(right.layer_order, right.source_order, right.sprite_index);
        });
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "storyboard preview index allocation failed"});
        return result;
    }
    result.index = std::move(candidate);
    return result;
}

auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
                               const storyboard_preview_index_t& index, std::int64_t time_ms,
                               storyboard_frame_arena_t& arena, storyboard_frame_options_t options)
    -> storyboard_frame_result_t {
    arena.reset();
    storyboard_frame_result_t result;
    const auto active_count = static_cast<std::size_t>(
        std::ranges::count_if(index.sources, [time_ms](const auto& source) {
            return source.begin_time_ms <= time_ms && time_ms <= source.end_time_ms;
        }));
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
    const auto allocation = arena.allocate_bytes(active_count * sizeof(storyboard_draw_instance_t),
                                                 alignof(storyboard_draw_instance_t));
    if (!allocation) {
        result.arena_error = allocation.error;
        return result;
    }
    auto* output = static_cast<storyboard_draw_instance_t*>(allocation.data);
    std::size_t output_count = 0;
    for (const auto& source : index.sources) {
        if (source.begin_time_ms > time_ms || time_ms > source.end_time_ms)
            continue;
        if (source.sprite_index >= document.sprites.size() ||
            document.sprites[source.sprite_index].stable_id != source.stable_id) {
            result.diagnostic =
                storyboard_diagnostic_t{storyboard_diagnostic_code_t::invalid_model, 0,
                                        "storyboard preview index is stale for its document"};
            return result;
        }
        const auto& sprite = document.sprites[source.sprite_index];
        const auto evaluate = [&](storyboard_opcode_t opcode) {
            return evaluate_storyboard_property(sprite, opcode, time_ms, options.maximum_nesting);
        };
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
        if (std::ranges::any_of(evaluations, [&](const auto* evaluation) {
                return evaluation_failed(*evaluation, result);
            })) {
            return result;
        }

        storyboard_draw_instance_t instance{
            .stable_id = sprite.stable_id,
            .source_order = sprite.source_order,
            .layer = sprite.layer,
            .origin = sprite.origin,
            .path = sprite.path,
            .x = sprite.x,
            .y = sprite.y,
        };
        if (move.value.present) {
            instance.x = move.value.values[0];
            instance.y = move.value.values[1];
        }
        if (newer_than(move_x, move))
            instance.x = move_x.value.values[0];
        if (newer_than(move_y, move))
            instance.y = move_y.value.values[0];
        if (scale.value.present) {
            instance.scale_x = scale.value.values[0];
            instance.scale_y = scale.value.values[0];
        }
        if (newer_than(vector_scale, scale)) {
            instance.scale_x = vector_scale.value.values[0];
            instance.scale_y = vector_scale.value.values[1];
        }
        if (fade.value.present)
            instance.opacity = fade.value.values[0];
        if (rotate.value.present)
            instance.rotation_radians = rotate.value.values[0];
        if (color.value.present) {
            instance.red = color.value.values[0];
            instance.green = color.value.values[1];
            instance.blue = color.value.values[2];
        }
        std::construct_at(output + output_count, instance);
        ++output_count;
    }
    result.instances = {output, output_count};
    return result;
}

} // namespace wormedit

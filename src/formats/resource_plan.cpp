#include <wormedit/formats/resource_plan.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/core/resource_assignment.h>

#include "../core/text_util.h"

#include <algorithm>
#include <map>

namespace wormedit {
namespace {
using error_type = error_t<failure_code_t>;

struct planner_t {
    const song_document_t& source;
    const song_document_t& source_profiles;
    format_family_t target;
    song_document_t reservations;
    resource_plan_t plan;
    std::map<std::string, asset_id_t> filenames;
    std::map<std::size_t, std::pair<asset_id_t, std::string>> explicit_claims;
    std::map<asset_id_t, std::string> hashes;

    auto reusable_binding(const resource_plan_request_t& request, asset_id_t asset,
        std::string_view filename) const -> const resource_plan_binding_t* {
        const auto previous = std::ranges::find_if(plan.bindings, [&](const auto& item) {
            return item.asset == asset && item.compatibility_key == request.compatibility_key &&
                item.filename == filename && (target != format_family_t::bmson ||
                    (item.source_resource == request.resource && item.chart == request.chart));
        });
        return previous == plan.bindings.end() ? nullptr : &*previous;
    }

    auto find_resource(resource_id_t resource) const -> const resource_entry_t* {
        const auto found = std::ranges::find(source.resources, resource, &resource_entry_t::id);
        return found == source.resources.end() ? nullptr : &*found;
    }

    auto filename_for(const resource_plan_request_t& request, const audio_asset_t& asset)
        -> result_t<std::string, error_type> {
        const auto path = request.filename ? text_util::path_from_utf8(*request.filename)
                                           : (asset.source_path.is_absolute() ? asset.source_path.filename() : asset.source_path);
        const auto name = text_util::path_utf8(path);
        if (!conversion_publication_plan_payload_t::can_track_path(path) ||
            name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
            return std::unexpected(error_type{failure_code_t::invalid_data, "Unsafe target audio filename"});
        const auto key = text_util::ascii_lowercase(name);
        const auto [found, inserted] = filenames.emplace(key, asset.id);
        if (!inserted && found->second != asset.id)
            return std::unexpected(error_type{failure_code_t::conflict,
                "Different audio or slice behavior would export to the same filename: " + name});
        return name;
    }

    auto bms_identifier(const resource_plan_request_t& request, const resource_entry_t& resource)
        -> result_t<std::pair<std::string, resource_id_t>, error_type> {
        std::optional<std::string> identifier = request.explicit_bms_index;
        if (!identifier) {
            for (const auto& identity : resource.target_identities) {
                if (bms_wav_slot_value(identity)) { identifier = identity.identifier; break; }
            }
        }
        if (!identifier) {
            const auto available = find_bms_identifier_run(reservations, "WAV", 1);
            if (!available)
                return std::unexpected(error_type{failure_code_t::capacity_exceeded, "BMS WAV slots are exhausted"});
            identifier = available->front();
        }
        const target_resource_identity_t identity{.family = format_family_t::bms,
            .identifier = *identifier, .namespace_name = "WAV"};
        const auto slot = bms_wav_slot_value(identity);
        if (!slot)
            return std::unexpected(error_type{failure_code_t::invalid_data, "BMS WAV index must be 01 through ZZ"});
        const auto owner = bms_wav_slot_owner(reservations, *slot);
        const auto* existing = find_resource(owner.resource);
        if (owner.ambiguous || (owner.resource && (!existing || existing->asset != resource.asset)))
            return std::unexpected(error_type{failure_code_t::conflict,
                "Explicit BMS WAV index refers to different or ambiguous audio: " + *identifier});
        const auto target_resource = owner.resource ? owner.resource : resource.id;
        auto found = std::ranges::find(reservations.resources, target_resource, &resource_entry_t::id);
        if (found != reservations.resources.end() && !std::ranges::contains(found->target_identities, identity))
            found->target_identities.push_back(identity);
        return std::pair{text_util::ascii_upper(*identifier), target_resource};
    }

    auto verify_source(const resource_plan_request_t& request, const audio_asset_t& bound)
        -> result_t<void, error_type> {
        if (request.expected_hash) {
            if (!text_util::is_sha256_digest(*request.expected_hash))
                return std::unexpected(error_type{failure_code_t::invalid_data, "Malformed declared audio hash"});
            const auto hash = text_util::ascii_lowercase(*request.expected_hash);
            const auto [found, inserted] = hashes.emplace(bound.id, hash);
            if (!inserted && found->second != hash)
                return std::unexpected(error_type{failure_code_t::conflict, "One audio asset has conflicting frozen hash promises"});
        }
        if (!request.explicit_bms_index || !request.source_chart) return {};
        const auto resolved = resolve_bms_sample_reference(source_profiles,
            {.index = *request.explicit_bms_index, .source_chart = request.source_chart});
        if (!resolved) return std::unexpected(resolved.error());
        if (resolved->second.source_path.lexically_normal() != bound.source_path.lexically_normal() ||
            resolved->second.stream != bound.stream)
            return std::unexpected(error_type{failure_code_t::conflict, "The promised source WAV binding now names different audio"});
        return {};
    }

    auto claim_explicit(const resource_plan_request_t& request, asset_id_t asset) -> result_t<void, error_type> {
        if (!request.explicit_bms_index) return {};
        const auto slot = bms_wav_slot_value({.family = format_family_t::bms,
            .identifier = *request.explicit_bms_index, .namespace_name = "WAV"});
        if (!slot) return std::unexpected(error_type{failure_code_t::invalid_data, "BMS WAV index must be 01 through ZZ"});
        const auto identity = std::pair{asset, request.compatibility_key};
        const auto [found, inserted] = explicit_claims.emplace(*slot, identity);
        if (!inserted && found->second != identity)
            return std::unexpected(error_type{failure_code_t::conflict, "Explicit WAV references claim different audio or slice behavior"});
        return {};
    }

    auto assign_target(resource_plan_binding_t& binding, const resource_plan_request_t& request,
        const resource_entry_t& resource) -> result_t<void, error_type> {
        if (target == format_family_t::bms) {
            const auto retained = std::ranges::any_of(resource.target_identities,
                [](const auto& identity) { return bms_wav_slot_value(identity).has_value(); });
            const auto* reusable = reusable_binding(request, binding.asset, binding.filename);
            if (!request.explicit_bms_index && !retained && reusable) {
                binding.identifier = reusable->identifier;
                binding.target_resource = reusable->target_resource;
                binding.decision = resource_plan_decision_t::reused;
                return {};
            }
            auto identifier = bms_identifier(request, resource);
            if (!identifier) return std::unexpected(identifier.error());
            binding.identifier = identifier->first;
            binding.target_resource = identifier->second;
            if (request.explicit_bms_index || retained) binding.decision = resource_plan_decision_t::retained;
        } else {
            const auto* previous = reusable_binding(request, binding.asset, binding.filename);
            if (previous) {
                binding.identifier = previous->identifier;
                binding.target_resource = previous->target_resource;
                binding.decision = resource_plan_decision_t::reused;
            } else if (target != format_family_t::bmson) binding.identifier = binding.filename;
        }
        return {};
    }

    auto prepare_bmson_candidate(const resource_plan_binding_t& binding) -> result_t<void, error_type> {
        auto* chart = reservations.find_chart(binding.chart);
        if (!chart) return std::unexpected(error_type{failure_code_t::not_found, "BMSON plan requires a prepared selected chart"});
        const auto resource = std::ranges::find(reservations.resources, binding.target_resource, &resource_entry_t::id);
        if (resource == reservations.resources.end()) return std::unexpected(error_type{failure_code_t::not_found, "Planned BMSON resource is unavailable"});
        resource->name = binding.filename;
        if (auto* asset = reservations.find_audio_asset(binding.asset)) asset->source_path = binding.filename;
        const auto update = [&](auto& event) {
            if (event.id == binding.event) { event.sound = binding.target_resource; event.sample_reference.reset(); }
        };
        for (auto& measure : chart->measures) {
            for (auto& note : measure.notes) update(note);
            for (auto& event : measure.bgm_events) update(event);
        }
        return {};
    }
    auto finalize_bmson() -> result_t<void, error_type> {
        std::map<chart_id_t, bmson_channel_plan_t> charts;
        for (const auto& binding : plan.bindings)
            if (const auto prepared = prepare_bmson_candidate(binding); !prepared) return prepared;
        for (auto& binding : plan.bindings) {
            auto found = charts.find(binding.chart);
            if (found == charts.end()) {
                auto channels = build_bmson_channel_plan(reservations, {.chart = binding.chart});
                if (!channels.value) return std::unexpected(error_type{failure_code_t::unsupported,
                    channels.diagnostics.empty() ? "BMSON channel preflight failed" : channels.diagnostics.front().message});
                found = charts.emplace(binding.chart, std::move(*channels.value)).first;
            }
            for (const auto& mapped : found->second.bindings)
                if (mapped.event == binding.event) binding.channel_indices.push_back(mapped.channel_index);
            if (binding.channel_indices.empty()) return std::unexpected(error_type{failure_code_t::not_found, "BMSON writer did not allocate the requested event"});
            binding.identifier = std::to_string(binding.channel_indices.front());
        }
        return {};
    }

    auto append(const resource_plan_request_t& request) -> result_t<void, error_type> {
        const auto* resource = find_resource(request.resource);
        if (!resource || resource->missing ||
            (!resource->has_role(resource_role_t::sample) && !resource->has_role(resource_role_t::bgm)))
            return std::unexpected(error_type{failure_code_t::not_found, "Sample reference has no playable catalog resource"});
        const auto* asset = source.find_audio_asset(resource->asset);
        if (!asset)
            return std::unexpected(error_type{failure_code_t::not_found, "Sample reference has no declared audio asset"});
        if (const auto verified = verify_source(request, *asset); !verified) return verified;
        if (const auto claimed = claim_explicit(request, asset->id); !claimed) return claimed;
        auto filename = filename_for(request, *asset);
        if (!filename) return std::unexpected(filename.error());
        resource_plan_binding_t binding{.chart = request.chart, .event = request.event,
            .source_resource = resource->id, .target_resource = resource->id, .asset = asset->id,
            .filename = *filename, .compatibility_key = request.compatibility_key,
            .decision = resource_plan_decision_t::allocated, .expected_hash = request.expected_hash};
        if (const auto assigned = assign_target(binding, request, *resource); !assigned)
            return std::unexpected(assigned.error());
        plan.bindings.push_back(std::move(binding));
        return {};
    }
};
} // namespace

auto resource_plan_t::from(const song_document_t& document, format_family_t target,
    std::span<const resource_plan_request_t> requests, const song_document_t* source_profiles)
    -> attempt_t<resource_plan_t, std::vector<resource_plan_diagnostic_t>> {
    if (target != format_family_t::bms && target != format_family_t::bmson && target != format_family_t::osu_mania)
        return {.diagnostics = {{.code = failure_code_t::unsupported, .message = "Unsupported sample resource target"}}};
    if (requests.size() > default_maximum_chart_objects)
        return {.diagnostics = {{.code = failure_code_t::capacity_exceeded, .message = "Sample resource request limit exceeded"}}};
    planner_t planner{.source = document, .source_profiles = source_profiles ? *source_profiles : document,
        .target = target, .reservations = document};
    planner.plan.bindings.reserve(requests.size());
    for (const auto& request : requests) {
        if (const auto result = planner.append(request); !result)
            return {.diagnostics = {{.code = result.error().code, .message = result.error().message,
                .location = {.chart = request.chart, .event = request.event}}}};
    }
    if (target == format_family_t::bmson)
        if (const auto channels = planner.finalize_bmson(); !channels)
            return {.diagnostics = {{.code = channels.error().code, .message = channels.error().message}}};
    return {.value = std::move(planner.plan)};
}
} // namespace wormedit

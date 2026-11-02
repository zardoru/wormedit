#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <span>

namespace wormedit {

// Resolved event input, in the caller's stable difficulty/event order. Retained
// explicit slots are promises; otherwise compatible catalog bindings may reuse.
struct resource_plan_request_t {
    chart_id_t chart;
    object_id_t event;
    resource_id_t resource;
    std::optional<std::string> explicit_bms_index;
    std::optional<std::string> filename;
    // Distinguishes target-incompatible slices/continuation/sample behavior.
    std::string compatibility_key;
    std::optional<chart_id_t> source_chart;
    std::optional<std::string> expected_hash;
    auto operator==(const resource_plan_request_t&) const -> bool = default;
};

enum class resource_plan_decision_t { retained, reused, allocated };
struct resource_plan_binding_t {
    chart_id_t chart;
    object_id_t event;
    resource_id_t source_resource;
    resource_id_t target_resource;
    asset_id_t asset;
    std::string identifier;
    std::string filename;
    std::string compatibility_key;
    resource_plan_decision_t decision{};
    std::vector<std::size_t> channel_indices; // BMSON: concrete zero-based output positions.
    std::optional<std::string> expected_hash;
    auto operator==(const resource_plan_binding_t&) const -> bool = default;
};
struct resource_plan_location_t {
    chart_id_t chart;
    object_id_t event;
    auto operator==(const resource_plan_location_t&) const -> bool = default;
};
using resource_plan_diagnostic_t = format_diagnostic_t<failure_code_t, resource_plan_location_t>;

// Pure allocation proposal over the same prepared candidate passed to the writer.
// Does not mutate the input document or access files.
// A failed proposal publishes no partial bindings. Call before serialization.
struct resource_plan_t {
    [[nodiscard]] static auto from(const song_document_t& document, format_family_t target,
        std::span<const resource_plan_request_t> requests, const song_document_t* source_profiles = nullptr)
        -> attempt_t<resource_plan_t, std::vector<resource_plan_diagnostic_t>>;
    std::vector<resource_plan_binding_t> bindings;
};

} // namespace wormedit

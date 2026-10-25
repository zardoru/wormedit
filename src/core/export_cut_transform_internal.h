#pragma once
#include <wormedit/core/export_cut_transform.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/bms_program_edit.h>
#include <unordered_map>
#include <unordered_set>

namespace wormedit::export_cut_detail {
struct transform_context_t {
    const song_document_t& source;
    const export_cut_timeline_t& timeline;
    std::span<const export_cut_source_t> sources;
    std::vector<export_cut_diagnostic_t>& diagnostics;
    std::optional<chart_id_t> chart;
    std::uint64_t object_ceiling{};
    std::size_t generated_objects{}, work_units{};
    [[nodiscard]] auto reserve_work(std::size_t count = 1) -> bool;
    [[nodiscard]] auto next_object() -> std::optional<object_id_t>;
    void report(diagnostic_severity_t severity, diagnostic_code_t code, std::string message,
                std::optional<object_id_t> object = {});
    [[nodiscard]] auto duration(resource_id_t resource) const -> std::optional<signed_rational_t>;
    [[nodiscard]] auto asset_duration(asset_id_t asset) const -> std::optional<signed_rational_t>;
    [[nodiscard]] auto is_sampled(resource_id_t resource) const -> bool;
    mutable std::unordered_set<std::uint64_t> sampled_resources;
    mutable bool media_indexed{};
    mutable std::unordered_map<std::uint64_t, std::optional<signed_rational_t>> asset_durations;
    mutable std::unordered_map<std::uint64_t, std::optional<signed_rational_t>> resource_durations;
    mutable std::unordered_map<std::string, std::optional<signed_rational_t>> custom_durations;
    void ensure_media_index() const;
    [[nodiscard]] auto custom_sound_duration(std::string_view filename) const -> std::optional<signed_rational_t>;
};
[[nodiscard]] auto transform_chart(const chart_document_t& source, transform_context_t& context)
    -> std::optional<chart_document_t>;
[[nodiscard]] auto transform_scoped(const bms_program_document_t& source, transform_context_t& context)
    -> std::shared_ptr<const bms_program_document_t>;
}

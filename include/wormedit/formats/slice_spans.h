#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace wormedit {

inline constexpr std::uint32_t slice_span_sample_rate = 44'100;

struct slice_span_source_t {
    asset_id_t asset;
    std::uint32_t native_sample_rate{};
    std::uint64_t native_frame_count{};
    std::uint64_t output_frame_count{};
    auto operator==(const slice_span_source_t&) const -> bool = default;
};

// All boundaries are frames in the normalized 44.1 kHz stereo decoder output.
struct slice_span_t {
    asset_id_t asset;
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    std::string filename;
    auto operator==(const slice_span_t&) const -> bool = default;
};

enum class slice_span_event_kind_t { sound_channel_event, note, bgm_event };
struct slice_span_mapping_t {
    slice_span_event_kind_t kind{};
    object_id_t event;
    // No index means no audio (unbound sound or valid continuation at/past EOF); gameplay remains.
    std::optional<std::size_t> span_index;
    auto operator==(const slice_span_mapping_t&) const -> bool = default;
};
struct slice_span_diagnostic_t {
    [[nodiscard]] auto is_error() const noexcept -> bool { return true; }
    auto operator==(const slice_span_diagnostic_t&) const -> bool = default;
    std::string message;
    std::optional<asset_id_t> asset;
    std::optional<object_id_t> event;
};
struct slice_span_options_t {
    std::size_t maximum_events{200'000};
    std::size_t maximum_spans{10'000};
    // Existing output names, compared without ASCII case, are skipped deterministically.
    std::vector<std::string> occupied_filenames;
};
// Pure, deterministic, bounded and detached. No filesystem access or document mutation.
// Sources supply decoded geometry, so final continuation spans are closed exactly.
struct slice_span_plan_payload_t {
    [[nodiscard]] static auto from(const song_document_t& song, chart_id_t chart,
        std::span<const slice_span_source_t> sources, const slice_span_options_t& options = {})
        -> diagnosed_t<slice_span_plan_payload_t, std::vector<slice_span_diagnostic_t>>;
    std::vector<slice_span_t> spans;
    std::vector<slice_span_mapping_t> mappings;
    std::vector<slice_span_source_t> sources;
    auto operator==(const slice_span_plan_payload_t&) const -> bool = default;
};
using slice_span_plan_outcome_t =
    diagnosed_t<slice_span_plan_payload_t, std::vector<slice_span_diagnostic_t>>;

// Operation-scoped borrowed geometry; no vector or document copies.
struct slice_span_render_input_t {
    const std::vector<slice_span_t>& spans;
    const std::vector<slice_span_mapping_t>& mappings;
    const std::vector<slice_span_source_t>& sources;
    bool has_errors{};
    [[nodiscard]] static auto from(const slice_span_plan_outcome_t& plan) -> slice_span_render_input_t {
        return {plan.value.spans, plan.value.mappings, plan.value.sources, !plan.ok()};
    }
};

struct slice_span_render_options_t {
    std::filesystem::path source_directory;
    std::filesystem::path output_directory;
    std::stop_token stop;
    std::uint64_t maximum_span_frames{10ULL * 60ULL * slice_span_sample_rate};
    bool preserve_float_samples{false};
};
struct slice_span_render_payload_t {
    bool succeeded{true};
    [[nodiscard]] auto is_success() const -> bool { return succeeded; }
    std::vector<std::filesystem::path> files{};
    std::string error{};
    std::optional<asset_id_t> asset{};
    [[nodiscard]] static auto from(std::string message, std::optional<asset_id_t> asset = {}) -> slice_span_render_payload_t {
        return {.succeeded = false, .files = {}, .error = std::move(message), .asset = asset};
    }
    auto operator==(const slice_span_render_payload_t&) const -> bool = default;
};
// Preflights every referenced decoder/span before writing; one render call per distinct span
// supports overlapping ranges. Exclusive publication never overwrites an existing file.
// Any failure removes our staged and published files and leaves the input unchanged.
[[nodiscard]] auto create_slice_span_files(const song_document_t& song, const slice_span_render_input_t& plan,
    const slice_span_render_options_t& options) -> slice_span_render_payload_t;


struct conversion_request_t;
struct conversion_result_t;
// Decoder-target helpers; unavailable in decoder-OFF builds. No destination writes
// occur during geometry preparation. Rendering must receive a private staging directory.
[[nodiscard]] auto prepare_conversion_slice_sources(const song_document_t& song,
    conversion_request_t& request, const std::filesystem::path& source_directory,
    std::stop_token stop = {}) -> std::optional<std::string>;
[[nodiscard]] auto render_conversion_slice_plans(const song_document_t& song,
    const conversion_result_t& converted, const slice_span_render_options_t& options)
    -> std::optional<std::string>;

} // namespace wormedit

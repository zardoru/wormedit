#include <wormedit/formats/slice_spans.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/audio/slice_render.h>
#include <wormedit/core/atomic_file_storage.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <system_error>
#include <tuple>

namespace wormedit {
namespace {

using decoders_t = std::map<std::uint64_t, std::unique_ptr<audio::decoder_t>>;

auto reject(slice_span_render_result_t& result, std::string message, std::optional<asset_id_t> asset = {}) -> bool {
    result.error = std::move(message);
    result.asset = asset;
    return false;
}

auto open_source(const song_document_t& song, const slice_span_plan_t& plan, asset_id_t id,
                 const slice_span_render_options_t& options, decoders_t& decoders,
                 slice_span_render_result_t& result) -> bool {
    if (decoders.contains(id.value)) return true;
    const auto asset = song.find_audio_asset(id);
    const auto geometry = std::ranges::find(plan.sources, id, &slice_span_source_t::asset);
    if (asset == nullptr || geometry == plan.sources.end() ||
        std::ranges::count(song.audio_assets, id, &audio_asset_t::id) != 1 ||
        std::ranges::count(plan.sources, id, &slice_span_source_t::asset) != 1)
        return reject(result, "Missing or ambiguous decoded audio asset " + std::to_string(id.value), id);
    // Keep at most one open decoder; conversion may reference thousands of keysounds.
    decoders.clear();
    auto decoder = std::make_unique<audio::decoder_t>();
    const auto path = asset->source_path.is_absolute() ? asset->source_path : options.source_directory / asset->source_path;
    std::string error;
    if (!decoder->open(path, &error)) return reject(result, "Cannot decode asset " + std::to_string(id.value) + " (" + path.string() + "): " + error, id);
    const auto info = decoder->info();
    if (!info || info->source_sample_rate != geometry->native_sample_rate ||
        info->source_frame_count != geometry->native_frame_count || info->output_frame_count != geometry->output_frame_count)
        return reject(result, "Decoded asset geometry changed: " + path.string(), id);
    decoders.emplace(id.value, std::move(decoder));
    return true;
}

auto verify_decoding(audio::decoder_t& decoder, const slice_span_t& span,
                     const slice_span_render_options_t& options, slice_span_render_result_t& result) -> bool {
    const auto info = decoder.info();
    if (!info || span.end_frame <= span.begin_frame || span.end_frame > info->output_frame_count ||
        span.end_frame - span.begin_frame > options.maximum_span_frames)
        return reject(result, "Invalid decoded range for asset " + std::to_string(span.asset.value), span.asset);
    std::array<float, audio::maximum_read_frames * audio::output_channel_count> buffer{};
    auto cursor = span.begin_frame;
    while (cursor < span.end_frame) {
        if (options.stop.stop_requested()) return reject(result, "Slice rendering cancelled", span.asset);
        const auto requested = static_cast<std::size_t>(std::min<std::uint64_t>(audio::maximum_read_frames, span.end_frame - cursor));
        const auto read = decoder.read_at(cursor, buffer, requested);
        if (read.status != audio::read_status_t::success || read.frames_read != requested)
            return reject(result, "Cannot decode span for asset " + std::to_string(span.asset.value), span.asset);
        cursor += read.frames_read;
    }
    return true;
}

auto plain_filename(const std::string& name) -> bool {
    const std::filesystem::path path{name};
    return !name.empty() && name.size() <= 240 && path == path.filename() && name != "." && name != ".." &&
        name.find_first_of("/\\") == std::string::npos &&
        std::ranges::none_of(name, [](unsigned char byte) { return byte < 0x20 || byte == 0x7f; });
}

auto preflight(const song_document_t& song, const slice_span_plan_t& plan,
               const slice_span_render_options_t& options, decoders_t& decoders,
               slice_span_render_result_t& result) -> bool {
    if (!plan || options.output_directory.empty() || plan.spans.size() > 10'000)
        return reject(result, "A valid bounded span plan and output directory are required");
    std::set<std::string> names;
    std::set<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> spans;
    for (const auto& span : plan.spans) {
        if (!plain_filename(span.filename) || !names.insert(span.filename).second ||
            !spans.emplace(span.asset.value, span.begin_frame, span.end_frame).second)
            return reject(result, "Invalid or duplicate span filename/identity", span.asset);
        std::error_code error;
        const auto existing = std::filesystem::symlink_status(options.output_directory / span.filename, error);
        if (std::filesystem::exists(existing)) return reject(result, "Output already exists: " + span.filename, span.asset);
        if (error && error != std::errc::no_such_file_or_directory) return reject(result, "Cannot inspect output: " + error.message(), span.asset);
        if (!open_source(song, plan, span.asset, options, decoders, result) ||
            !verify_decoding(*decoders.at(span.asset.value), span, options, result)) return false;
    }
    for (const auto& mapping : plan.mappings)
        if (mapping.span_index && *mapping.span_index >= plan.spans.size()) return reject(result, "Span event mapping is out of bounds");
    return true;
}

struct publication_t {
    std::filesystem::path staging;
    std::filesystem::path output;
    bool created_output{};
    std::vector<std::filesystem::path> published;
    ~publication_t() {
        std::error_code ignored;
        for (const auto& path : published) std::filesystem::remove(path, ignored);
        if (!staging.empty()) std::filesystem::remove_all(staging, ignored);
        if (created_output) std::filesystem::remove(output, ignored);
    }
};

auto make_staging(publication_t& publication, slice_span_render_result_t& result) -> bool {
    std::error_code error;
    publication.created_output = std::filesystem::create_directories(publication.output, error);
    if (error) return reject(result, "Cannot create output directory: " + error.message());
    static std::atomic<std::uint64_t> next{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto candidate = publication.output / (".wormedit-slice-stage-" + std::to_string(next++));
        if (std::filesystem::create_directory(candidate, error)) {
            publication.staging = candidate;
            return true;
        }
        if (error && error != std::errc::file_exists) return reject(result, "Cannot create slice staging: " + error.message());
    }
    return reject(result, "Cannot reserve an exclusive slice staging directory");
}

auto render_staged(const song_document_t& song, const slice_span_plan_t& plan, const slice_span_render_options_t& options,
                   decoders_t& decoders, const publication_t& publication,
                   std::vector<std::filesystem::path>& staged, slice_span_render_result_t& result) -> bool {
    for (std::size_t index = 0; index < plan.spans.size(); ++index) {
        const auto& span = plan.spans[index];
        const auto geometry = std::ranges::find(plan.sources, span.asset, &slice_span_source_t::asset);
        if (options.preserve_float_samples && geometry != plan.sources.end() &&
            geometry->native_sample_rate != slice_span_sample_rate) decoders.clear();
        if (!open_source(song, plan, span.asset, options, decoders, result)) return false;
        const std::array<audio_slice_range_t, 1> range{{{span.begin_frame, span.end_frame}}};
        auto rendered = audio::render_audio_slices(*decoders.at(span.asset.value), range,
            {.directory = publication.staging, .base_name = "span" + std::to_string(index), .stop = options.stop, .progress = {},
             .maximum_slice_frames = options.maximum_span_frames,
             .encoding = options.preserve_float_samples ? audio::slice_wav_encoding_t::float32
                                                        : audio::slice_wav_encoding_t::pcm16});
        if (!rendered) return reject(result, "Cannot render asset " + std::to_string(span.asset.value) + ": " + rendered.error, span.asset);
        staged.push_back(std::move(rendered.files.front()));
    }
    return true;
}

auto publish(const slice_span_plan_t& plan, const slice_span_render_options_t& options,
             std::span<const std::filesystem::path> staged, publication_t& publication,
             slice_span_render_result_t& result) -> bool {
    for (std::size_t index = 0; index < plan.spans.size(); ++index) {
        if (options.stop.stop_requested()) return reject(result, "Slice publication cancelled");
        auto path = options.output_directory / plan.spans[index].filename;
        std::error_code error;
        // The staging and output directories share a filesystem. An exclusive hard link
        // publishes complete WAV bytes and refuses destination races without overwriting.
        std::filesystem::create_hard_link(staged[index], path, error);
        if (error) return reject(result, "Cannot exclusively publish " + path.string() + ": " + error.message(), plan.spans[index].asset);
        publication.published.push_back(std::move(path));
    }
    result.files = std::move(publication.published);
    publication.created_output = false;
    result.completed = true;
    return true;
}

} // namespace

auto render_slice_span_plan(const song_document_t& song, const slice_span_plan_t& plan,
    const slice_span_render_options_t& options) -> slice_span_render_result_t {
    slice_span_render_result_t result;
    decoders_t decoders;
    if (!preflight(song, plan, options, decoders, result)) return result;
    if (plan.spans.empty()) {
        result.completed = true;
        return result;
    }
    publication_t publication{.staging = {}, .output = options.output_directory, .created_output = false, .published = {}};
    publication.published.reserve(plan.spans.size());
    if (!make_staging(publication, result)) return result;
    std::vector<std::filesystem::path> staged;
    staged.reserve(plan.spans.size());
    if (!render_staged(song, plan, options, decoders, publication, staged, result)) return result;
    publish(plan, options, staged, publication, result);
    return result;
}

namespace {
auto conversion_chart_selected(chart_id_t id, const conversion_request_t& request) -> bool {
    if (request.chart) return id == *request.chart;
    return request.charts.empty() || std::ranges::find(request.charts, id) != request.charts.end();
}

void collect_slice_resource_assets(const song_document_t& song, const chart_document_t& chart,
                                  std::set<asset_id_t>& assets) {
    auto append = [&](const auto& event) {
        if (!event.sound || event.sound_channel_event) return;
        const auto resource = std::ranges::find(song.resources, *event.sound, &resource_entry_t::id);
        if (resource != song.resources.end()) assets.insert(resource->asset);
    };
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) append(note);
        for (const auto& event : measure.bgm_events) append(event);
    }
}

auto conversion_slice_assets(const song_document_t& song, const conversion_request_t& request)
    -> std::set<asset_id_t> {
    std::set<asset_id_t> assets;
    for (const auto& chart : song.charts) {
        if (chart.source_family != format_family_t::bmson || !conversion_chart_selected(chart.id, request)) continue;
        for (const auto& channel : chart.sound_channels)
            if (!channel.events.empty()) assets.insert(channel.source_clip.source);
        collect_slice_resource_assets(song, chart, assets);
    }
    return assets;
}
auto copy_conversion_asset(const song_document_t& song, const conversion_asset_copy_t& copy,
    const slice_span_render_options_t& options) -> std::optional<std::string> {
    std::filesystem::path source;
    const auto audio = song.find_audio_asset(copy.asset);
    const auto visual = std::ranges::find(song.visual_assets, copy.asset, &visual_asset_t::id);
    if (audio != nullptr) source = audio->source_path;
    else if (visual != song.visual_assets.end()) source = visual->source_path;
    else return "Missing retained export asset " + std::to_string(copy.asset.value);
    if (!plain_filename(copy.relative_path.generic_string())) return "Unsafe retained asset filename";
    if (options.stop.stop_requested()) return "Asset copying cancelled";
    if (!source.is_absolute()) source = options.source_directory / source;
    std::error_code error;
    const auto bytes = std::filesystem::file_size(source, error);
    if (error || bytes > 10ULL * 1024ULL * 1024ULL * 1024ULL) return "Retained asset unavailable or exceeds copy limit: " + source.string();
    if (!std::filesystem::copy_file(source, options.output_directory / copy.relative_path,
            std::filesystem::copy_options::none, error) || error)
        return "Cannot stage retained asset: " + source.string();
    return std::nullopt;
}
} // namespace

auto prepare_conversion_slice_sources(const song_document_t& song, conversion_request_t& request,
    const std::filesystem::path& source_directory, std::stop_token stop) -> std::optional<std::string> {
    std::vector<slice_span_source_t> sources;
    for (const auto id : conversion_slice_assets(song, request)) {
        if (stop.stop_requested()) return "Slice geometry preparation cancelled";
        const auto asset = song.find_audio_asset(id);
        if (asset == nullptr) return "Missing audio asset " + std::to_string(id.value);
        audio::decoder_t decoder;
        std::string error;
        const auto path = asset->source_path.is_absolute() ? asset->source_path : source_directory / asset->source_path;
        if (!decoder.open(path, &error)) return "Cannot decode " + path.string() + ": " + error;
        const auto info = decoder.info();
        if (!info) return "Missing decoded geometry for " + path.string();
        sources.push_back({id, info->source_sample_rate, info->source_frame_count, info->output_frame_count});
    }
    request.slice_sources = std::move(sources);
    return std::nullopt;
}

auto render_conversion_slice_plans(const song_document_t& song, const conversion_result_t& converted,
    const slice_span_render_options_t& options) -> std::optional<std::string> {
    for (const auto& name : converted.silent_files) {
        if (options.stop.stop_requested()) return "cancelled";
        const std::array<float, 2> samples{};
        const auto bytes = audio::encode_wav_float32(samples, slice_span_sample_rate);
        const auto saved = save_file_atomic(options.output_directory / name, bytes, bytes.size());
        if (!saved) return "Could not stage silent gameplay sample";
    }
    for (const auto& copy : converted.asset_copies)
        if (auto failure = copy_conversion_asset(song, copy, options)) return failure;
    auto rendering = options;
    rendering.preserve_float_samples = true;
    for (const auto& plan : converted.slice_plans) {
        const auto rendered = render_slice_span_plan(song, plan, rendering);
        if (!rendered) return rendered.error;
    }
    return std::nullopt;
}

} // namespace wormedit

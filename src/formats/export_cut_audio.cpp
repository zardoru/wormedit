#include <wormedit/formats/export_cut_audio.h>
#include <wormedit/audio/slice_render.h>
#include <wormedit/audio/stream_encoder.h>
#include <wormedit/core/atomic_file_storage.h>
#include <wormedit/core/export_cut_frames.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/metadata_inheritance.h>
#include "../core/wide_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace wormedit {
namespace {
using render_step_t = result_t<void, slice_span_render_payload_t>;

auto source_path(const audio_asset_t& asset, const std::filesystem::path& root) -> std::filesystem::path {
    return asset.source_path.is_absolute() ? asset.source_path : root / asset.source_path;
}

auto audio_resource(const resource_entry_t& resource) -> bool {
    return resource.has_role(resource_role_t::music) || resource.has_role(resource_role_t::bgm) ||
        resource.has_role(resource_role_t::sample);
}

auto source_evidence(const song_document_t& song, asset_id_t id, const std::filesystem::path& root)
    -> result_t<export_cut_source_t, std::string> {
    const auto* asset = song.find_audio_asset(id);
    if (!asset || std::ranges::count(song.audio_assets, id, &audio_asset_t::id) != 1)
        return std::unexpected("Missing or ambiguous export cut audio asset " + std::to_string(id.value));
    audio::decoder_t decoder;
    std::string message;
    if (!decoder.open(source_path(*asset, root), &message, audio::decoder_index_t::skip))
        return std::unexpected("Cannot decode export cut audio: " + message);
    const auto info = decoder.info();
    if (!info || !info->source_sample_rate || !info->source_frame_count || !info->output_frame_count)
        return std::unexpected("Export cut audio has unknown or empty decoded geometry");
    return export_cut_source_t{.asset = id, .native_sample_rate = info->source_sample_rate,
        .native_frame_count = info->source_frame_count, .output_frame_count = info->output_frame_count,
        .source_path = asset->source_path,
        .bitrate_bits_per_second = audio::stream_encoder_t::source_bitrate(source_path(*asset, root))};
}

void include_custom_note_assets(const song_document_t& song, const chart_document_t& chart,
    std::set<asset_id_t>& assets) {
    std::set<std::filesystem::path> paths;
    for (const auto& measure : chart.measures) for (const auto& note : measure.notes)
        if (note.osu_hitsample && !note.osu_hitsample->filename.empty())
            paths.insert(std::filesystem::path{note.osu_hitsample->filename}.lexically_normal());
    for (const auto& asset : song.audio_assets)
        if (paths.contains(asset.source_path.lexically_normal())) assets.insert(asset.id);
}

auto referenced_cut_assets(const song_document_t& song) -> diagnosed_t<std::set<asset_id_t>> {
    diagnosed_t<std::set<asset_id_t>> result;
    for (const auto& resource : song.resources) if (audio_resource(resource)) result.value.insert(resource.asset);
    for (const auto& chart : song.read_charts()) {
        include_custom_note_assets(song, *chart, result.value);
        auto overrides = chart->metadata.overrides;
        overrides.value(inherited_metadata_field_t::preview).reset();
        const auto references = metadata_audio_references_t::from(song, overrides);
        result.value.insert(references.value.assets.begin(), references.value.assets.end());
        result.diagnostics.insert(result.diagnostics.end(), references.diagnostics.begin(), references.diagnostics.end());
        for (const auto& channel : chart->sound_channels) result.value.insert(channel.source_clip.source);
    }
    return result;
}

// Sequential decoder reads retain its resampler state across both kept and
// discarded source regions. Only one bounded chunk and its next frame are held.
struct source_reader_t {
    audio::decoder_t& decoder;
    std::uint64_t frame_count;
    std::array<float, audio::maximum_read_frames * 2> samples{};
    std::uint64_t begin{};
    std::size_t count{};
    std::array<float, 2> previous{};
    std::stop_token stop;

    auto frame(std::uint64_t position) -> result_t<std::array<float, 2>, std::string> {
        position = std::min(position, frame_count - 1);
        while (position >= begin + count) {
            if (stop.stop_requested()) return std::unexpected("Export cut rendering cancelled");
            if (count) previous = {samples[(count - 1) * 2], samples[(count - 1) * 2 + 1]};
            begin += count;
            const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(audio::maximum_read_frames, frame_count - begin));
            const auto read = decoder.read_frames(samples, request);
            if (!read.is_success() || read.frames_read != request || !request)
                return std::unexpected("Cannot decode the complete export cut source");
            count = request;
        }
        if (position < begin) return previous;
        const auto index = static_cast<std::size_t>(position - begin) * 2;
        return std::array{samples[index], samples[index + 1]};
    }

    auto canonical_frame(std::uint64_t position) -> result_t<std::array<float, 2>, std::string> {
        // 44100 / 48000 = 147 / 160; quotient form cannot overflow.
        const auto whole = position / 160 * 147 + position % 160 * 147 / 160;
        const auto fraction = static_cast<float>(position % 160 * 147 % 160) / 160.0F;
        const auto a = frame(whole);
        if (!a) return std::unexpected(a.error());
        const auto b = frame(whole + 1);
        if (!b) return std::unexpected(b.error());
        return std::array{(*a)[0] + ((*b)[0] - (*a)[0]) * fraction,
            (*a)[1] + ((*b)[1] - (*a)[1]) * fraction};
    }
};

auto write_bytes(std::FILE* file, const std::string& bytes) -> bool {
    return !bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
}

auto write_interval(source_reader_t& reader, std::FILE* file, audio::stream_encoder_t* encoder,
    const export_cut_frame_interval_t& interval)
    -> render_step_t {
    std::array<float, audio::maximum_read_frames * 2> output{};
    auto cursor = interval.begin_frame;
    while (cursor < interval.end_frame) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(audio::maximum_read_frames, interval.end_frame - cursor));
        for (std::size_t index = 0; index < count; ++index) {
            const auto frame = reader.canonical_frame(cursor + index);
            if (!frame) return std::unexpected(slice_span_render_payload_t::from(frame.error()));
            std::copy(frame->begin(), frame->end(), output.begin() + static_cast<std::ptrdiff_t>(index * 2));
        }
        const auto pcm = std::span{output}.first(count * 2);
        const auto written = encoder ? encoder->write(pcm, reader.stop)
            : write_bytes(file, audio::encode_wav_float32_data(pcm));
        if (!written) return std::unexpected(slice_span_render_payload_t::from("Cannot encode export cut audio"));
        cursor += count;
    }
    return {};
}

auto canonical_length(const export_cut_source_t& source) -> std::optional<std::uint64_t> {
    if (!source.native_sample_rate || source.native_frame_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return {};
    return sample_frame_at({static_cast<std::int64_t>(source.native_frame_count), source.native_sample_rate},
        export_cut_render_sample_rate).frame;
}

auto open_plan(const song_document_t& song, const export_cut_audio_plan_t& plan,
    const slice_span_render_options_t& options, audio::decoder_t& decoder) -> render_step_t {
    const auto* asset = song.find_audio_asset(plan.asset);
    if (!asset || plan.source.asset != plan.asset ||
        std::ranges::count(song.audio_assets, plan.asset, &audio_asset_t::id) != 1)
        return std::unexpected(slice_span_render_payload_t::from("Missing or ambiguous cut audio asset", plan.asset));
    std::string message;
    if (!decoder.open(source_path(*asset, options.source_directory), &message))
        return std::unexpected(slice_span_render_payload_t::from("Cannot decode cut audio: " + message, plan.asset));
    const auto info = decoder.info();
    if (!info || !info->output_frame_count || info->source_sample_rate != plan.source.native_sample_rate ||
        info->source_frame_count != plan.source.native_frame_count || info->output_frame_count != plan.source.output_frame_count)
        return std::unexpected(slice_span_render_payload_t::from("Export cut decoded geometry changed", plan.asset));
    return {};
}

auto plain_filename(const std::string& name) -> bool {
    const auto path = std::filesystem::path{name};
    return !name.empty() && name.size() <= 240 && name != "." && name != ".." &&
        path == path.filename() && name.find_first_of("/\\") == std::string::npos &&
        std::ranges::none_of(name, [](unsigned char c) { return c < 32 || c == 127; });
}

struct rendered_files_t {
    std::vector<std::filesystem::path> paths;
    ~rendered_files_t() {
        std::error_code ignored;
        for (const auto& path : paths) std::filesystem::remove(path, ignored);
    }
};

auto exceeds_render_duration(std::uint64_t output_frames, std::uint64_t decoder_frame_limit) -> bool {
    return wide_math::u128_less(wide_math::multiply(decoder_frame_limit, export_cut_render_sample_rate),
        wide_math::multiply(output_frames, slice_span_sample_rate));
}

auto render_plan(const song_document_t& song, const export_cut_audio_plan_t& plan,
    const export_cut_timeline_t& timeline, const slice_span_render_options_t& options,
    rendered_files_t& files) -> render_step_t {
    const auto length = canonical_length(plan.source);
    if (!length) return std::unexpected(slice_span_render_payload_t::from("Invalid export cut source clock", plan.asset));
    const auto frames = export_cut_frame_timeline_t::from(timeline, export_cut_render_sample_rate, *length);
    const auto header = audio::encode_wav_float32_header(plan.output_frames, export_cut_render_sample_rate);
    if (!frames.ok() || plan.output_frames != frames.value.kept_frame_count(0, *length) ||
        exceeds_render_duration(plan.output_frames, options.maximum_span_frames) || header.empty())
        return std::unexpected(slice_span_render_payload_t::from("Invalid or oversized export cut audio plan", plan.asset));
    audio::decoder_t decoder;
    auto opened = open_plan(song, plan, options, decoder);
    if (!opened) return opened;
    const auto path = options.output_directory / plan.filename;
    auto file = open_exclusive_file(path);
    if (!file) return std::unexpected(slice_span_render_payload_t::from("Cannot exclusively create cut audio: " + path.string(), plan.asset));
    files.paths.push_back(path);
    audio::stream_encoder_t encoder;
    const auto compressed = plan.encoding.codec != export_audio_codec_t::wav;
    if (compressed) {
        file.reset();
        if (!encoder.open(path, plan.encoding, export_cut_render_sample_rate))
            return std::unexpected(slice_span_render_payload_t::from("Cannot initialize cut audio encoder (MP3 requires FFmpeg with libmp3lame)", plan.asset));
    } else if (!write_bytes(file.get(), header))
        return std::unexpected(slice_span_render_payload_t::from("Cannot write cut WAV header", plan.asset));
    source_reader_t reader{.decoder = decoder, .frame_count = plan.source.output_frame_count, .stop = options.stop};
    for (const auto& interval : frames.value.kept_intervals(0, *length)) {
        auto written = write_interval(reader, file.get(), compressed ? &encoder : nullptr, interval);
        if (!written) return written;
    }
    const auto tail = reader.frame(plan.source.output_frame_count - 1);
    if (!tail) return std::unexpected(slice_span_render_payload_t::from(tail.error(), plan.asset));
    if (compressed ? !encoder.finish() : std::fclose(file.release()) != 0)
        return std::unexpected(slice_span_render_payload_t::from("Cannot finish cut audio file", plan.asset));
    return {};
}
} // namespace

auto prepare_export_cut_sources(const song_document_t& song, const std::filesystem::path& root,
    std::stop_token stop) -> diagnosed_t<std::vector<export_cut_source_t>> {
    diagnosed_t<std::vector<export_cut_source_t>> result;
    if (song.export_cuts.empty()) return result;
    auto assets = referenced_cut_assets(song);
    result.diagnostics = std::move(assets.diagnostics);
    for (const auto id : assets.value) {
        if (stop.stop_requested()) {
            result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, "Export cut source inspection cancelled"));
            break;
        }
        auto source = source_evidence(song, id, root);
        if (!source) result.diagnostics.push_back(error(diagnostic_code_t::dangling_asset, source.error()));
        else result.value.push_back(*source);
    }
    return result;
}

auto render_export_cut_audio_plans(const song_document_t& song, std::span<const export_cut_audio_plan_t> plans,
    const export_cut_timeline_t& timeline, const slice_span_render_options_t& options) -> slice_span_render_payload_t {
    if (plans.empty()) return {};
    if (options.output_directory.empty() || plans.size() > 10'000)
        return slice_span_render_payload_t::from("A bounded cut audio plan and private staging directory are required");
    std::error_code error;
    std::filesystem::create_directories(options.output_directory, error);
    if (error) return slice_span_render_payload_t::from("Cannot create cut audio staging: " + error.message());
    std::set<std::string> names;
    rendered_files_t files;
    for (const auto& plan : plans) {
        if (options.stop.stop_requested()) return slice_span_render_payload_t::from("Export cut rendering cancelled");
        if (!plan.encoding.is_valid() || !plain_filename(plan.filename) || !names.insert(plan.filename).second ||
            std::filesystem::path{plan.filename}.extension() != plan.encoding.extension())
            return slice_span_render_payload_t::from("Invalid or duplicate cut audio filename", plan.asset);
        auto rendered = render_plan(song, plan, timeline, options, files);
        if (!rendered) return std::move(rendered.error());
    }
    return {.files = std::exchange(files.paths, {})};
}

auto cut_preview_audio(std::span<const float> samples, std::uint64_t begin,
    const export_cut_timeline_t& timeline, std::uint32_t rate) -> diagnosed_t<std::vector<float>> {
    diagnosed_t<std::vector<float>> result;
    if (samples.size() % 2 || samples.size() / 2 > std::numeric_limits<std::uint64_t>::max() - begin) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution, "Invalid bounded preview PCM interval"));
        return result;
    }
    const auto end = begin + samples.size() / 2;
    auto frames = export_cut_frame_timeline_t::from(timeline, rate, end);
    if (!frames.ok()) { result.diagnostics = std::move(frames.diagnostics); return result; }
    result.value.reserve(static_cast<std::size_t>(frames.value.kept_frame_count(begin, end)) * 2);
    for (const auto& interval : frames.value.kept_intervals(begin, end)) {
        const auto from = static_cast<std::size_t>(interval.begin_frame - begin) * 2;
        const auto count = static_cast<std::size_t>(interval.end_frame - interval.begin_frame) * 2;
        result.value.insert(result.value.end(), samples.begin() + static_cast<std::ptrdiff_t>(from),
            samples.begin() + static_cast<std::ptrdiff_t>(from + count));
    }
    return result;
}
} // namespace wormedit

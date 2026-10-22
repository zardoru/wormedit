#include "sys/audio/audio_project_import.h"
#include "core/text_util.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif
#if defined(WORMEDIT_HAS_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}
#endif

#include <array>
#include <fstream>
#include <memory>
#include <string_view>

namespace wormedit::sys {
namespace {

#if defined(WORMEDIT_HAS_FFMPEG)
constexpr std::size_t maximum_tag_bytes = 1024U * 1024U;
constexpr std::size_t maximum_field_bytes = 16U * 1024U;

struct avformat_input_closer_t {
    void operator()(AVFormatContext* input) const noexcept { avformat_close_input(&input); }
};

auto metadata_text(const AVDictionary* metadata, const char* key) -> std::string {
    const auto* entry = av_dict_get(metadata, key, nullptr, 0);
    if (!entry || !entry->value) return {};
    std::size_t length{};
    while (length <= maximum_field_bytes && entry->value[length] != '\0') ++length;
    const auto value = std::string_view{entry->value, length};
    if (!text_util::is_valid_bounded_text(value, maximum_field_bytes)) return {};
    return std::string{text_util::trim(value, " \t\r\n")};
}

void fill_metadata(const AVDictionary* metadata, audio_project_import_result_t& result) {
    if (result.title.empty()) result.title = metadata_text(metadata, "title");
    if (result.artist.empty()) result.artist = metadata_text(metadata, "artist");
}

void read_metadata(const std::filesystem::path& path, audio_project_import_result_t& result) {
    const auto filename = text_util::path_utf8(path);
    auto* input = avformat_alloc_context();
    if (!input) return;
    input->probesize = maximum_tag_bytes;
    input->max_analyze_duration = AV_TIME_BASE;
    if (avformat_open_input(&input, filename.c_str(), nullptr, nullptr) < 0) {
        avformat_close_input(&input);
        return;
    }
    const std::unique_ptr<AVFormatContext, avformat_input_closer_t> owned{input};
    if (avformat_find_stream_info(input, nullptr) < 0) return;
    fill_metadata(input->metadata, result);
    for (unsigned index = 0; index < input->nb_streams; ++index)
        fill_metadata(input->streams[index]->metadata, result);
}

void prefill_metadata(const std::filesystem::path& path, audio_project_import_result_t& result) {
    read_metadata(path, result);
    if (result.title.empty()) result.title = text_util::path_utf8(path.stem());
    if (result.title.empty() || !text_util::is_valid_bounded_text(result.title, maximum_field_bytes))
        result.title = "Untitled";
}
#endif

auto can_read_audio_file(const std::filesystem::path& path) -> bool {
    std::ifstream stream{path, std::ios::binary};
    return stream && stream.peek() != std::char_traits<char>::eof();
}

#if !defined(WORMEDIT_HAS_AUDIO_DECODER)
auto has_audio_signature(const std::filesystem::path& path, std::string_view extension) -> bool {
    std::ifstream stream{path, std::ios::binary};
    std::array<char, 4> prefix{};
    stream.read(prefix.data(), prefix.size());
    if (stream.gcount() == 0) return false;
    const auto bytes = std::string_view{prefix.data(), static_cast<std::size_t>(stream.gcount())};
    return extension == ".ogg" ? bytes == "OggS"
        : bytes.starts_with("ID3") || static_cast<unsigned char>(prefix.front()) == 0xffU;
}
#endif

} // namespace

auto inspect_audio_for_project(const std::filesystem::path& source_path)
    -> audio_project_import_result_t {
    audio_project_import_result_t result;
    result.source_path = source_path;
    const auto extension = text_util::lowercase_extension(source_path);
    if (extension != ".mp3" && extension != ".ogg") {
        result.error = "Select an MP3 or Ogg Vorbis audio file.";
        return result;
    }
    if (!can_read_audio_file(source_path)) {
        result.error = "Could not read the audio file.";
        return result;
    }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::decoder_t decoder;
    if (!decoder.open(source_path, &result.error)) return result;
    const auto info = decoder.info();
    if (!info || info->source_sample_rate == 0) {
        result.error = "Could not inspect the audio stream.";
        return result;
    }
    result.stream = {.sample_rate = info->source_sample_rate,
                     .channels = info->source_channel_count,
                     .frame_count = info->source_frame_count,
                     .seekable = info->seekable};
    result.duration_seconds = static_cast<double>(info->source_frame_count) /
                              static_cast<double>(info->source_sample_rate);
#else
    if (!has_audio_signature(source_path, extension)) {
        result.error = "The file does not appear to contain supported audio.";
        return result;
    }
#endif
#if defined(WORMEDIT_HAS_FFMPEG)
    prefill_metadata(source_path, result);
#endif
    result.succeeded = true;
    result.error.clear();
    return result;
}

} // namespace wormedit::sys

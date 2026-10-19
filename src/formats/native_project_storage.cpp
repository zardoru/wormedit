#include "../core/text_util.h"
#include <wormedit/formats/native_project_storage.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace wormedit {
namespace {

using json = nlohmann::json;

constexpr std::string_view recovery_format = "wormedit-recovery";
constexpr std::uint64_t recovery_format_version = 1;

// The envelope records the document schema of its payload. The payload is a native project, so any
// schema the project reader can migrate (1-5) or read directly (6) is acceptable here.
constexpr auto is_recovery_schema_readable(std::uint64_t schema) -> bool {
    return schema >= 1 && schema <= 9;
}
constexpr std::size_t maximum_diagnostics = 8;
constexpr std::size_t maximum_diagnostic_message_bytes = 240;
constexpr std::size_t maximum_recovery_path_bytes = 4096;
constexpr std::size_t recovery_envelope_overhead_limit = 2 * maximum_recovery_path_bytes + 8192;

auto maximum_recovery_envelope_bytes(std::size_t maximum_payload_bytes) -> std::size_t;

struct expected_document_t {
    song_id_t id;
    std::uint64_t revision{};
};

auto to_version(const song_document_t& song) -> native_project_document_version_t {
    return {.project_id = song.id, .revision = song.revision};
}

auto path_to_utf8(const std::filesystem::path& path) -> std::string {
    return text_util::path_utf8(path);
}

using text_util::path_from_utf8;

auto bounded_message(std::string message) -> std::string {
    if (message.size() > maximum_diagnostic_message_bytes) {
        message.resize(maximum_diagnostic_message_bytes - 3);
        message += "...";
    }
    return message;
}

void add_diagnostic(std::vector<native_project_storage_diagnostic_t>& diagnostics,
                    native_project_storage_diagnostic_code_t code, std::string message,
                    const std::filesystem::path& path = {},
                    diagnostic_severity_t severity = diagnostic_severity_t::error) {
    if (diagnostics.size() == maximum_diagnostics) {
        return;
    }
    diagnostics.push_back({.severity = severity,
                           .code = code,
                           .message = bounded_message(std::move(message)),
                           .path = path});
}

auto is_stage_allowed(const native_project_storage_options_t& options,
                   native_project_storage_stage_t stage, const std::filesystem::path& path) noexcept
    -> bool {
    if (!options.allow_stage) {
        return true;
    }
    try {
        return options.allow_stage(stage, path);
    } catch (...) {
        return false;
    }
}

auto stage_failure(native_project_storage_stage_t stage) -> std::string {
    switch (stage) {
    case native_project_storage_stage_t::serialize:
        return "injected native-project serialization failure";
    case native_project_storage_stage_t::preflight_validate:
        return "injected native-project preflight validation failure";
    case native_project_storage_stage_t::checksum:
        return "injected recovery checksum failure";
    case native_project_storage_stage_t::create_temporary:
        return "injected temporary-file creation failure";
    case native_project_storage_stage_t::write_temporary:
        return "injected temporary-file write failure";
    case native_project_storage_stage_t::flush_temporary:
        return "injected temporary-file flush failure";
    case native_project_storage_stage_t::close_temporary:
        return "injected temporary-file close failure";
    case native_project_storage_stage_t::reopen_temporary:
        return "injected temporary-file re-open failure";
    case native_project_storage_stage_t::validate_reopened:
        return "injected re-opened project validation failure";
    case native_project_storage_stage_t::replace_destination:
        return "injected destination replacement failure";
    case native_project_storage_stage_t::sync_destination_directory:
        return "injected destination directory sync failure";
    }
    return "injected native-project storage failure";
}

auto as_error(const native_project_diagnostic_t& diagnostic)
    -> native_project_storage_diagnostic_t {
    auto code = native_project_storage_diagnostic_code_t::validation_failure;
    if (diagnostic.code == native_project_diagnostic_code_t::resource_limit) {
        code = native_project_storage_diagnostic_code_t::resource_limit;
    } else if (diagnostic.code == native_project_diagnostic_code_t::serialization_failure) {
        code = native_project_storage_diagnostic_code_t::serialization_failure;
    }
    return {.severity = diagnostic.severity,
            .code = code,
            .message = bounded_message(diagnostic.message),
            .path = {}};
}

void copy_codec_diagnostics(std::vector<native_project_storage_diagnostic_t>& destination,
                            const std::vector<native_project_diagnostic_t>& source) {
    for (const auto& diagnostic : source) {
        if (destination.size() == maximum_diagnostics) {
            return;
        }
        destination.push_back(as_error(diagnostic));
    }
}

auto is_document_current(const song_document_t& song, expected_document_t expected) -> bool {
    return song.id == expected.id && song.revision == expected.revision;
}

// SHA-256 keeps the recovery checksum self-contained and available in the
// headless core on all three desktop platforms.
auto sha256(std::string_view input) -> std::string {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};
    std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::vector<std::uint8_t> data;
    data.reserve(input.size() + 72);
    for (const auto byte : input) {
        data.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }
    const auto bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) {
        data.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    const auto rotate_right = [](std::uint32_t value, unsigned amount) {
        return (value >> amount) | (value << (32U - amount));
    };
    for (std::size_t block = 0; block < data.size(); block += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = block + index * 4;
            words[index] = (static_cast<std::uint32_t>(data[offset]) << 24U) |
                           (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
                           (static_cast<std::uint32_t>(data[offset + 2]) << 8U) |
                           static_cast<std::uint32_t>(data[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15], 7) ^
                            rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3U);
            const auto s1 = rotate_right(words[index - 2], 17) ^
                            rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10U);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        auto a = hash[0];
        auto b = hash[1];
        auto c = hash[2];
        auto d = hash[3];
        auto e = hash[4];
        auto f = hash[5];
        auto g = hash[6];
        auto h = hash[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temp1 = h + sum1 + choice + constants[index] + words[index];
            const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto word : hash) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back(digits[(word >> shift) & 0x0fU]);
        }
    }
    return result;
}

auto strict_unsigned(const json& value) -> std::uint64_t {
    if (!value.is_number_unsigned()) {
        throw std::invalid_argument("recovery envelope integer is not unsigned");
    }
    return value.get<std::uint64_t>();
}

auto strict_signed(const json& value) -> std::int64_t {
    if (value.is_number_integer() && !value.is_number_unsigned()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(number);
        }
    }
    throw std::invalid_argument("recovery envelope timestamp is outside the signed range");
}

auto recovery_envelope(const song_document_t& song, std::string_view payload,
                       const native_project_recovery_metadata_t& metadata,
                       const native_project_storage_options_t& options,
                       std::vector<native_project_storage_diagnostic_t>& diagnostics)
    -> std::optional<std::string> {
    if (!is_stage_allowed(options, native_project_storage_stage_t::checksum, {})) {
        add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::checksum_failure,
                       stage_failure(native_project_storage_stage_t::checksum));
        return std::nullopt;
    }
    const auto original_path = path_to_utf8(metadata.original_source_path);
    const auto intended_path = path_to_utf8(metadata.intended_project_path);
    if (original_path.size() > maximum_recovery_path_bytes ||
        intended_path.size() > maximum_recovery_path_bytes) {
        add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::resource_limit,
                       "recovery path metadata exceeds its configured byte limit");
        return std::nullopt;
    }
    try {
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   metadata.utc_write_time.time_since_epoch())
                                   .count();
        json root{{"format", recovery_format},
                  {"format_version", recovery_format_version},
                  {"schema_version", song.schema_version},
                  {"project_id", song.id.value},
                  {"revision", song.revision},
                  {"last_explicit_save_revision", metadata.last_explicit_save_revision
                                                      ? json(*metadata.last_explicit_save_revision)
                                                      : json(nullptr)},
                  {"session_sequence", metadata.session_sequence},
                  {"source_path", original_path},
                  {"intended_project_path", intended_path},
                  {"utc_write_time_unix_ms", timestamp},
                  {"payload_length", payload.size()},
                  {"payload_sha256", sha256(payload)},
                  {"payload", payload}};
        auto bytes = root.dump(2);
        bytes.push_back('\n');
        const auto maximum_bytes =
            maximum_recovery_envelope_bytes(options.codec_limits.maximum_source_bytes);
        if (bytes.size() > maximum_bytes) {
            add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::resource_limit,
                           "recovery envelope exceeds its configured byte limit");
            return std::nullopt;
        }
        return bytes;
    } catch (const std::exception& exception) {
        add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::serialization_failure,
                       std::string("recovery envelope serialization failed: ") + exception.what());
        return std::nullopt;
    }
}

auto validate_native_payload(std::string_view bytes, expected_document_t expected,
                             native_project_limits_t limits,
                             std::vector<native_project_storage_diagnostic_t>& diagnostics,
                             const std::filesystem::path& path = {}) -> bool {
    const auto parsed = parse_native_project(bytes, limits);
    if (!parsed.value.has_value() || !parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostics.size() == maximum_diagnostics) {
                break;
            }
            auto converted = as_error(diagnostic);
            converted.path = path;
            diagnostics.push_back(std::move(converted));
        }
        if (parsed.diagnostics.empty()) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::validation_failure,
                           "native-project payload did not produce a validated document", path);
        }
        return false;
    }
    if (!is_document_current(*parsed.value, expected)) {
        add_diagnostic(
            diagnostics, native_project_storage_diagnostic_code_t::identity_mismatch,
            "validated native project ID or revision differs from the requested snapshot", path);
        return false;
    }
    return true;
}

struct parsed_recovery_t {
    native_project_recovery_snapshot_t snapshot;
};

class recovery_envelope_sax_t final : public nlohmann::json_sax<json> {
  public:
    explicit recovery_envelope_sax_t(std::size_t maximum_payload_bytes)
        : maximum_payload_bytes_(maximum_payload_bytes) {}

    auto null() -> bool override {
        return count_node();
    }
    auto boolean(bool) -> bool override {
        return count_node();
    }
    auto number_integer(number_integer_t) -> bool override {
        return count_node();
    }
    auto number_unsigned(number_unsigned_t) -> bool override {
        return count_node();
    }
    auto number_float(number_float_t, const string_t&) -> bool override {
        return count_node();
    }
    auto string(string_t& value) -> bool override {
        if (value.size() > maximum_payload_bytes_) {
            limit_exceeded_ = true;
            return false;
        }
        return count_node();
    }
    auto binary(binary_t&) -> bool override {
        return count_node();
    }
    auto start_object(std::size_t) -> bool override {
        return start_container();
    }
    auto key(string_t& value) -> bool override {
        constexpr std::array<std::string_view, 13> keys{"format",
                                                        "format_version",
                                                        "schema_version",
                                                        "project_id",
                                                        "revision",
                                                        "last_explicit_save_revision",
                                                        "session_sequence",
                                                        "source_path",
                                                        "intended_project_path",
                                                        "utc_write_time_unix_ms",
                                                        "payload_length",
                                                        "payload_sha256",
                                                        "payload"};
        const auto key_view = std::string_view{value};
        if (value.size() > 64 || std::ranges::find(keys, key_view) == keys.end() ||
            std::ranges::find(seen_keys_, value) != seen_keys_.end()) {
            malformed_ = true;
            return false;
        }
        seen_keys_.push_back(value);
        return true;
    }
    auto end_object() -> bool override {
        if (depth_ > 0) {
            --depth_;
        }
        return true;
    }
    auto start_array(std::size_t) -> bool override {
        return start_container();
    }
    auto end_array() -> bool override {
        if (depth_ > 0) {
            --depth_;
        }
        return true;
    }
    auto parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&)
        -> bool override {
        malformed_ = !limit_exceeded_;
        return false;
    }

    [[nodiscard]] auto is_limit_exceeded() const noexcept -> bool {
        return limit_exceeded_;
    }

  private:
    auto count_node() -> bool {
        if (nodes_ == 32) {
            limit_exceeded_ = true;
            return false;
        }
        ++nodes_;
        return true;
    }
    auto start_container() -> bool {
        if (!count_node() || depth_ == 1) {
            limit_exceeded_ = true;
            return false;
        }
        ++depth_;
        return true;
    }

    std::size_t maximum_payload_bytes_{};
    std::size_t nodes_{};
    std::size_t depth_{};
    std::vector<std::string> seen_keys_;
    bool limit_exceeded_{};
    bool malformed_{};
};

auto maximum_recovery_envelope_bytes(std::size_t maximum_payload_bytes) -> std::size_t {
    constexpr auto multiplier = std::size_t{3};
    if (maximum_payload_bytes >
        (std::numeric_limits<std::size_t>::max() - recovery_envelope_overhead_limit) / multiplier) {
        return std::numeric_limits<std::size_t>::max();
    }
    return maximum_payload_bytes * multiplier + recovery_envelope_overhead_limit;
}

auto parse_recovery_impl(std::string_view bytes, native_project_limits_t limits,
                         std::vector<native_project_storage_diagnostic_t>& diagnostics,
                         const std::filesystem::path& path = {})
    -> std::optional<parsed_recovery_t> {
    const auto maximum_bytes = maximum_recovery_envelope_bytes(limits.maximum_source_bytes);
    if (bytes.size() > maximum_bytes) {
        add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::resource_limit,
                       "recovery envelope exceeds its configured byte limit", path);
        return std::nullopt;
    }
    recovery_envelope_sax_t sax{limits.maximum_source_bytes};
    if (!json::sax_parse(bytes.begin(), bytes.end(), &sax)) {
        add_diagnostic(
            diagnostics,
            sax.is_limit_exceeded() ? native_project_storage_diagnostic_code_t::resource_limit
                                 : native_project_storage_diagnostic_code_t::validation_failure,
            sax.is_limit_exceeded() ? "recovery envelope exceeds its node, depth, or string limit"
                                 : "recovery envelope JSON is malformed",
            path);
        return std::nullopt;
    }
    try {
        const auto root = json::parse(bytes);
        if (!root.is_object() || root.size() != 13 ||
            root.value("format", std::string{}) != recovery_format ||
            strict_unsigned(root.at("format_version")) != recovery_format_version ||
            !is_recovery_schema_readable(strict_unsigned(root.at("schema_version")))) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::validation_failure,
                           "recovery envelope schema is unsupported or malformed", path);
            return std::nullopt;
        }
        const auto project_id = strict_unsigned(root.at("project_id"));
        const auto revision = strict_unsigned(root.at("revision"));
        const auto session_sequence = strict_unsigned(root.at("session_sequence"));
        const auto payload_length = strict_unsigned(root.at("payload_length"));
        const auto& payload = root.at("payload").get_ref<const std::string&>();
        const auto& checksum = root.at("payload_sha256").get_ref<const std::string&>();
        const auto& source_path = root.at("source_path").get_ref<const std::string&>();
        const auto& intended_path = root.at("intended_project_path").get_ref<const std::string&>();
        if (project_id == 0 || payload_length != payload.size() ||
            source_path.size() > maximum_recovery_path_bytes ||
            intended_path.size() > maximum_recovery_path_bytes || checksum.size() != 64) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::validation_failure,
                           "recovery envelope identity, length, or metadata is invalid", path);
            return std::nullopt;
        }
        if (sha256(payload) != checksum) {
            add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::checksum_mismatch,
                           "recovery payload checksum does not match", path);
            return std::nullopt;
        }
        auto parsed_song = parse_native_project(payload, limits);
        if (!parsed_song.value.has_value() || !parsed_song.ok()) {
            for (const auto& diagnostic : parsed_song.diagnostics) {
                if (diagnostics.size() == maximum_diagnostics) {
                    break;
                }
                auto converted = as_error(diagnostic);
                converted.path = path;
                diagnostics.push_back(std::move(converted));
            }
            return std::nullopt;
        }
        const expected_document_t expected{.id = song_id_t{project_id}, .revision = revision};
        if (!is_document_current(*parsed_song.value, expected)) {
            add_diagnostic(
                diagnostics, native_project_storage_diagnostic_code_t::identity_mismatch,
                "recovery envelope ID or revision differs from its native-project payload", path);
            return std::nullopt;
        }
        std::optional<std::uint64_t> saved_revision;
        const auto& saved = root.at("last_explicit_save_revision");
        if (!saved.is_null()) {
            saved_revision = strict_unsigned(saved);
        }
        if (saved_revision.has_value() && *saved_revision > revision) {
            add_diagnostic(
                diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
                "recovery envelope saved revision is newer than its document revision", path);
            return std::nullopt;
        }
        const auto timestamp =
            std::chrono::milliseconds{strict_signed(root.at("utc_write_time_unix_ms"))};
        const auto earliest_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::duration::min())
                                            .count();
        const auto latest_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::duration::max())
                                          .count();
        if (timestamp.count() < earliest_timestamp || timestamp.count() > latest_timestamp) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::validation_failure,
                           "recovery envelope timestamp is outside the system clock range", path);
            return std::nullopt;
        }
        return parsed_recovery_t{
            .snapshot = {
                .song = std::move(*parsed_song.value),
                .metadata =
                    {
                        .original_source_path = path_from_utf8(source_path),
                        .intended_project_path = path_from_utf8(intended_path),
                        .last_explicit_save_revision = saved_revision,
                        .utc_write_time =
                            std::chrono::system_clock::time_point{
                                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                    timestamp)},
                        .session_sequence = session_sequence,
                    },
            }};
    } catch (const std::exception& exception) {
        add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
                       std::string("recovery envelope is malformed: ") + exception.what(), path);
        return std::nullopt;
    }
}

constexpr std::size_t absolute_maximum_scan_candidates = 64;
constexpr std::size_t absolute_maximum_scan_directory_entries = 1024;
constexpr std::size_t absolute_maximum_scan_file_bytes = 256U * 1024U * 1024U;
constexpr std::size_t absolute_maximum_scan_total_bytes = 512U * 1024U * 1024U;
constexpr std::size_t absolute_maximum_scan_diagnostics = 64;

void add_scan_diagnostic(std::vector<native_project_storage_diagnostic_t>& diagnostics,
                         std::size_t maximum_diagnostics,
                         native_project_storage_diagnostic_code_t code, std::string message,
                         const std::filesystem::path& path = {},
                         diagnostic_severity_t severity = diagnostic_severity_t::warning) {
    if (diagnostics.size() >= maximum_diagnostics) {
        return;
    }
    diagnostics.push_back({.severity = severity,
                           .code = code,
                           .message = bounded_message(std::move(message)),
                           .path = path});
}

auto is_recovery_kind(native_project_recovery_candidate_kind_t kind) -> bool {
    return kind != native_project_recovery_candidate_kind_t::primary;
}

auto temporary_prefix(const std::filesystem::path& destination) -> std::string {
    return path_to_utf8(destination.filename()) + ".wormedit-tmp-";
}

auto compare_versions(native_project_document_version_t left,
                      native_project_document_version_t right)
    -> native_project_recovery_comparison_t {
    if (left.project_id != right.project_id) {
        return native_project_recovery_comparison_t::different_project;
    }
    if (left.revision < right.revision) {
        return native_project_recovery_comparison_t::older_revision;
    }
    if (left.revision > right.revision) {
        return native_project_recovery_comparison_t::newer_revision;
    }
    return native_project_recovery_comparison_t::equal_revision;
}

auto temporary_source_rank(native_project_recovery_candidate_kind_t kind) -> unsigned {
    switch (kind) {
    case native_project_recovery_candidate_kind_t::adjacent_backup:
        return 0;
    case native_project_recovery_candidate_kind_t::caller_supplied_entry:
        return 1;
    case native_project_recovery_candidate_kind_t::stale_primary_temporary:
    case native_project_recovery_candidate_kind_t::stale_recovery_temporary:
        return 2;
    case native_project_recovery_candidate_kind_t::primary:
        return 3;
    }
    return 3;
}

auto recovery_candidate_precedes(const native_project_recovery_candidate_t& left,
                                 const native_project_recovery_candidate_t& right) -> bool {
    if (!left.version.has_value() || !right.version.has_value() ||
        left.version->project_id != right.version->project_id) {
        return false;
    }
    if (left.version->revision != right.version->revision) {
        return left.version->revision > right.version->revision;
    }
    const auto left_rank = temporary_source_rank(left.kind);
    const auto right_rank = temporary_source_rank(right.kind);
    if (left_rank != right_rank) {
        return left_rank < right_rank;
    }
    const auto left_sequence = left.recovery_metadata.has_value()
                                   ? left.recovery_metadata->session_sequence
                                   : std::uint64_t{};
    const auto right_sequence = right.recovery_metadata.has_value()
                                    ? right.recovery_metadata->session_sequence
                                    : std::uint64_t{};
    if (left_sequence != right_sequence) {
        return left_sequence > right_sequence;
    }
    const auto left_time = left.recovery_metadata.has_value()
                               ? left.recovery_metadata->utc_write_time
                               : std::chrono::system_clock::time_point{};
    const auto right_time = right.recovery_metadata.has_value()
                                ? right.recovery_metadata->utc_write_time
                                : std::chrono::system_clock::time_point{};
    return left_time > right_time;
}

#ifdef _WIN32
using native_file_t = HANDLE;
const native_file_t invalid_file = INVALID_HANDLE_VALUE;
#else
using native_file_t = int;
constexpr native_file_t invalid_file = -1;
#endif

struct temporary_file_t {
    std::filesystem::path path;
    native_file_t handle{invalid_file};
    bool keep{};

    ~temporary_file_t() {
        close_ignoring_errors();
        if (!keep && !path.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    auto close_ignoring_errors() noexcept -> void {
        if (handle == invalid_file) {
            return;
        }
#ifdef _WIN32
        CloseHandle(handle);
#else
        ::close(handle);
#endif
        handle = invalid_file;
    }
};

auto process_id() -> std::uint64_t {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

auto make_temporary_path(const std::filesystem::path& destination, std::uint64_t sequence)
    -> std::filesystem::path {
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    auto name = destination.filename();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    name += ".wormedit-tmp-" + std::to_string(process_id()) + "-" + std::to_string(ticks) + "-" +
            std::to_string(sequence);
    return parent / name;
}

auto create_exclusive_file(const std::filesystem::path& path, native_file_t& handle,
                           int& error_value) -> bool {
#ifdef _WIN32
    handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error_value = static_cast<int>(GetLastError());
        return false;
    }
#else
    handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (handle < 0) {
        error_value = errno;
        return false;
    }
#endif
    return true;
}

auto write_all(native_file_t handle, std::string_view bytes, int& error_value) -> bool {
    std::size_t offset{};
    while (offset < bytes.size()) {
#ifdef _WIN32
        const auto remaining = std::min<std::size_t>(bytes.size() - offset, 1U << 20U);
        DWORD written{};
        if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(remaining), &written,
                       nullptr) ||
            written == 0) {
            error_value = static_cast<int>(GetLastError());
            return false;
        }
        offset += written;
#else
        const auto written = ::write(handle, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            error_value = errno;
            return false;
        }
        offset += static_cast<std::size_t>(written);
#endif
    }
    return true;
}

auto flush_file(native_file_t handle, int& error_value) -> bool {
#ifdef _WIN32
    if (!FlushFileBuffers(handle)) {
        error_value = static_cast<int>(GetLastError());
        return false;
    }
#else
    while (::fsync(handle) != 0) {
        if (errno == EINTR) {
            continue;
        }
        error_value = errno;
        return false;
    }
#endif
    return true;
}

auto close_file(native_file_t& handle, int& error_value) -> bool {
#ifdef _WIN32
    const auto result = CloseHandle(handle) != 0;
    if (!result) {
        error_value = static_cast<int>(GetLastError());
    }
#else
    const auto result = ::close(handle) == 0;
    if (!result) {
        error_value = errno;
    }
#endif
    handle = invalid_file;
    return result;
}

auto read_file_exact(const std::filesystem::path& path, std::size_t expected_size,
                     std::string& bytes, std::string& failure) -> bool {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        failure = "could not determine temporary-file size: " + error.message();
        return false;
    }
    if (size != expected_size) {
        failure = "temporary-file length changed before validation";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        failure = "could not re-open temporary file for validation";
        return false;
    }
    bytes.resize(expected_size);
    if (expected_size != 0) {
        stream.read(bytes.data(), static_cast<std::streamsize>(expected_size));
    }
    if (!stream || static_cast<std::size_t>(stream.gcount()) != expected_size) {
        failure = "could not read the complete temporary file for validation";
        return false;
    }
    return true;
}

auto replace_destination(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination, bool replace_existing,
                         std::string& failure) -> bool {
#ifdef _WIN32
    const auto flags = replace_existing ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                                        : MOVEFILE_WRITE_THROUGH;
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), flags)) {
        failure = "atomic destination replacement failed with Windows error " +
                  std::to_string(GetLastError());
        return false;
    }
#else
    // A same-directory hard-link install is atomic and fails if the target
    // already exists. The temporary remains owned by its RAII cleanup object.
    const auto installed = replace_existing ? ::rename(temporary.c_str(), destination.c_str())
                                            : ::link(temporary.c_str(), destination.c_str());
    if (installed != 0) {
        failure = "atomic destination replacement failed: " +
                  std::error_code(errno, std::generic_category()).message();
        return false;
    }
#endif
    return true;
}

auto sync_directory(const std::filesystem::path& destination, std::string& failure) -> bool {
#ifdef _WIN32
    (void)destination;
    (void)failure;
    return true;
#else
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const auto directory = ::open(parent.c_str(), flags);
    if (directory < 0) {
        failure = "could not open destination directory for sync: " +
                  std::error_code(errno, std::generic_category()).message();
        return false;
    }
    const auto sync_result = ::fsync(directory);
    const auto saved_error = errno;
    ::close(directory);
    if (sync_result != 0) {
        failure = "could not sync destination directory: " +
                  std::error_code(saved_error, std::generic_category()).message();
        return false;
    }
    return true;
#endif
}

auto move_no_replace(const std::filesystem::path& source, const std::filesystem::path& destination,
                     std::string& failure) -> bool {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        failure =
            "could not move recovery artifact with Windows error " + std::to_string(GetLastError());
        return false;
    }
#elif defined(__APPLE__)
#ifndef RENAME_EXCL
#define RENAME_EXCL 0x00000004
#endif
    if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) != 0) {
        failure = "could not move recovery artifact: " +
                  std::error_code(errno, std::generic_category()).message();
        return false;
    }
#elif defined(__linux__) && defined(SYS_renameat2)
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0U)
#endif
    if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(),
                  RENAME_NOREPLACE) != 0) {
        if (errno != ENOSYS && errno != EINVAL) {
            failure = "could not move recovery artifact: " +
                      std::error_code(errno, std::generic_category()).message();
            return false;
        }
        if (::link(source.c_str(), destination.c_str()) != 0) {
            failure = "could not stage recovery artifact: " +
                      std::error_code(errno, std::generic_category()).message();
            return false;
        }
        if (::unlink(source.c_str()) != 0) {
            const auto saved_error = errno;
            (void)::unlink(destination.c_str());
            failure = "could not finish recovery artifact move: " +
                      std::error_code(saved_error, std::generic_category()).message();
            return false;
        }
    }
#else
    // The fallback still refuses an existing target. The caller verifies the
    // moved content and attempts restoration if the source changed meanwhile.
    if (::link(source.c_str(), destination.c_str()) != 0) {
        failure = "could not stage recovery artifact: " +
                  std::error_code(errno, std::generic_category()).message();
        return false;
    }
    if (::unlink(source.c_str()) != 0) {
        const auto saved_error = errno;
        (void)::unlink(destination.c_str());
        failure = "could not finish recovery artifact move: " +
                  std::error_code(saved_error, std::generic_category()).message();
        return false;
    }
#endif
    return true;
}

auto reserve_temporary_file(const std::filesystem::path& destination,
                            const native_project_storage_options_t& options,
                            temporary_file_t& temporary,
                            std::vector<native_project_storage_diagnostic_t>& diagnostics) -> bool {
    static std::atomic<std::uint64_t> temporary_sequence{1};
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto sequence = temporary_sequence.fetch_add(1, std::memory_order_relaxed);
        temporary.path = make_temporary_path(destination, sequence);
        if (!is_stage_allowed(options, native_project_storage_stage_t::create_temporary,
                           temporary.path)) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::temporary_file_failure,
                           stage_failure(native_project_storage_stage_t::create_temporary),
                           destination);
            return false;
        }
        int error{};
        if (create_exclusive_file(temporary.path, temporary.handle, error)) {
            return true;
        }
#ifdef _WIN32
        const auto collision = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
#else
        const auto collision = error == EEXIST;
#endif
        if (!collision) {
            add_diagnostic(diagnostics,
                           native_project_storage_diagnostic_code_t::temporary_file_failure,
                           "could not create same-directory temporary file: " +
                               std::error_code(error,
#ifdef _WIN32
                                               std::system_category()
#else
                                               std::generic_category()
#endif
                                                   )
                                   .message(),
                           destination);
            return false;
        }
    }
    add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::temporary_file_failure,
                   "could not reserve a unique same-directory temporary file", destination);
    return false;
}

template <typename validator_t>
auto publish_prevalidated_bytes(const std::filesystem::path& destination, std::string_view bytes,
                                const native_project_storage_options_t& options,
                                validator_t&& validate_reopened, bool replace_existing = true)
    -> native_project_storage_outcome_t {
    native_project_storage_outcome_t result{};
    if (destination.empty() || destination.filename().empty()) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::invalid_destination,
                       "destination must name a file", destination);
        return result;
    }

    temporary_file_t temporary;
    if (!reserve_temporary_file(destination, options, temporary, result.diagnostics)) {
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::write_temporary, temporary.path)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       stage_failure(native_project_storage_stage_t::write_temporary),
                       temporary.path);
        return result;
    }
    int file_error{};
    if (!write_all(temporary.handle, bytes, file_error)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       "could not write complete temporary file: " +
                           std::error_code(file_error,
#ifdef _WIN32
                                           std::system_category()
#else
                                           std::generic_category()
#endif
                                               )
                               .message(),
                       temporary.path);
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::flush_temporary, temporary.path)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::flush_failure,
                       stage_failure(native_project_storage_stage_t::flush_temporary),
                       temporary.path);
        return result;
    }
    file_error = 0;
    if (!flush_file(temporary.handle, file_error)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::flush_failure,
                       "could not flush temporary file: " + std::error_code(file_error,
#ifdef _WIN32
                                                                            std::system_category()
#else
                                                                            std::generic_category()
#endif
                                                                                )
                                                                .message(),
                       temporary.path);
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::close_temporary, temporary.path)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       stage_failure(native_project_storage_stage_t::close_temporary),
                       temporary.path);
        return result;
    }
    file_error = 0;
    if (!close_file(temporary.handle, file_error)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       "could not close temporary file: " + std::error_code(file_error,
#ifdef _WIN32
                                                                            std::system_category()
#else
                                                                            std::generic_category()
#endif
                                                                                )
                                                                .message(),
                       temporary.path);
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::reopen_temporary, temporary.path)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::read_failure,
                       stage_failure(native_project_storage_stage_t::reopen_temporary),
                       temporary.path);
        return result;
    }
    std::string reopened_bytes;
    std::string read_failure;
    if (!read_file_exact(temporary.path, bytes.size(), reopened_bytes, read_failure)) {
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::read_failure,
                       std::move(read_failure), temporary.path);
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::validate_reopened,
                       temporary.path)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::validate_reopened), temporary.path);
        return result;
    }
    if (!validate_reopened(reopened_bytes, result.diagnostics, temporary.path)) {
        return result;
    }

    if (!is_stage_allowed(options, native_project_storage_stage_t::replace_destination, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::replacement_failure,
            stage_failure(native_project_storage_stage_t::replace_destination), destination);
        return result;
    }
    std::string replace_failure;
    if (!replace_destination(temporary.path, destination, replace_existing, replace_failure)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::replacement_failure,
                       std::move(replace_failure), destination);
        return result;
    }
    temporary.keep = replace_existing;
    result.value = true;
    if (!replace_existing) {
        std::error_code cleanup_error;
        (void)std::filesystem::remove(temporary.path, cleanup_error);
        if (!cleanup_error) {
            temporary.path.clear();
        } else {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::write_failure,
                           "destination was published, but its temporary link could not be removed",
                           temporary.path, diagnostic_severity_t::warning);
        }
    }

    std::string directory_failure;
    if (!is_stage_allowed(options, native_project_storage_stage_t::sync_destination_directory,
                       destination) ||
        !sync_directory(destination, directory_failure)) {
        if (directory_failure.empty()) {
            directory_failure =
                stage_failure(native_project_storage_stage_t::sync_destination_directory);
        }
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       std::string(replace_existing ? "destination was replaced, but "
                                                    : "destination was published, but ") +
                           directory_failure,
                       destination, diagnostic_severity_t::warning);
    }
    return result;
}

} // namespace

auto native_project_adjacent_backup_path(const std::filesystem::path& primary)
    -> std::filesystem::path {
    auto filename = primary.filename();
    filename += ".bak";
    return primary.has_parent_path() ? primary.parent_path() / filename : filename;
}

auto parse_native_project_recovery_snapshot(std::string_view bytes, native_project_limits_t limits)
    -> native_project_recovery_parse_outcome_t {
    native_project_recovery_parse_outcome_t result{};
    const auto parsed = parse_recovery_impl(bytes, limits, result.diagnostics);
    if (parsed.has_value()) {
        result.value = parsed->snapshot;
    }
    return result;
}

namespace {

struct recovery_candidate_spec_t {
    std::filesystem::path path;
    native_project_recovery_candidate_kind_t kind;
};

struct discovered_recovery_entry_t {
    std::filesystem::path path;
    native_project_recovery_candidate_kind_t kind{
        native_project_recovery_candidate_kind_t::caller_supplied_entry};
};

struct recovery_collection_state_t {
    std::vector<native_project_storage_diagnostic_t>& diagnostics;
    std::size_t maximum_candidates{};
    std::size_t maximum_diagnostics{};
    std::size_t maximum_directory_entries{};
    std::vector<recovery_candidate_spec_t> specs{};
    std::vector<std::filesystem::path> completed_recovery_entries{};
    bool candidate_limit_reported{false};
    bool inputs_within_limit{true};
};

auto add_recovery_spec(recovery_collection_state_t& state, const std::filesystem::path& path,
                       native_project_recovery_candidate_kind_t kind) -> bool {
    if (path.empty()) return true;
    const auto normalized = path.lexically_normal();
    if (std::ranges::any_of(state.specs, [&](const auto& spec) {
            return spec.path.lexically_normal() == normalized;
        }))
        return true;
    if (state.specs.size() >= state.maximum_candidates) {
        if (!state.candidate_limit_reported) {
            add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                                native_project_storage_diagnostic_code_t::resource_limit,
                                "recovery scan stopped at its candidate limit", path);
            state.candidate_limit_reported = true;
        }
        return false;
    }
    state.specs.push_back({.path = path, .kind = kind});
    return true;
}

auto inspect_caller_recovery_directory(const std::filesystem::path& directory,
                                       recovery_collection_state_t& state)
    -> std::vector<discovered_recovery_entry_t> {
    std::error_code error;
    std::filesystem::directory_iterator iterator{directory, error};
    if (error) {
        add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not inspect caller recovery directory: " + error.message(),
                            directory);
        return {};
    }
    std::vector<discovered_recovery_entry_t> discovered;
    discovered.reserve(std::min(state.maximum_candidates, std::size_t{16}));
    std::size_t examined{};
    for (const std::filesystem::directory_iterator end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                                native_project_storage_diagnostic_code_t::read_failure,
                                "could not continue caller recovery directory scan: " +
                                    error.message(),
                                directory);
            break;
        }
        if (++examined > state.maximum_directory_entries) {
            state.inputs_within_limit = false;
            discovered.clear();
            add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                                native_project_storage_diagnostic_code_t::resource_limit,
                                "caller recovery directory exceeds its entry limit; no partial "
                                "candidate set was retained",
                                directory);
            break;
        }
        std::error_code type_error;
        const auto status = iterator->symlink_status(type_error);
        if (type_error || status.type() != std::filesystem::file_type::regular) continue;
        const auto filename = path_to_utf8(iterator->path().filename());
        if (filename.find(".wormedit-quarantine-") != std::string::npos) continue;
        if (filename.find(".wormedit-crash-marker") != std::string::npos) continue;
        const auto kind = filename.find(".wormedit-tmp-") != std::string::npos
                              ? native_project_recovery_candidate_kind_t::stale_recovery_temporary
                              : native_project_recovery_candidate_kind_t::caller_supplied_entry;
        discovered.push_back({.path = iterator->path(), .kind = kind});
    }
    std::ranges::sort(discovered, {}, [](const auto& entry) {
        return path_to_utf8(entry.path.lexically_normal());
    });
    return discovered;
}

void collect_recovery_primary_paths(const native_project_recovery_scan_request_t& request,
                                    recovery_collection_state_t& state) {
    if (request.primary_path.empty()) return;
    (void)add_recovery_spec(state, request.primary_path,
                            native_project_recovery_candidate_kind_t::primary);
    (void)add_recovery_spec(state, native_project_adjacent_backup_path(request.primary_path),
                            native_project_recovery_candidate_kind_t::adjacent_backup);
}

void collect_caller_recovery_entries(const native_project_recovery_scan_request_t& request,
                                     recovery_collection_state_t& state) {
    const auto count = std::min(request.caller_supplied_entries.size(), state.maximum_candidates);
    if (request.caller_supplied_entries.size() > count) {
        state.inputs_within_limit = false;
        add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::resource_limit,
                            "recovery scan stopped at its caller-entry limit");
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& path = request.caller_supplied_entries[index];
        if (!add_recovery_spec(state, path,
                               native_project_recovery_candidate_kind_t::caller_supplied_entry)) {
            state.inputs_within_limit = false;
            break;
        }
        state.completed_recovery_entries.push_back(path);
    }
}

void collect_caller_recovery_directories(const native_project_recovery_scan_request_t& request,
                                         recovery_collection_state_t& state) {
    const auto count =
        std::min(request.caller_supplied_directories.size(), state.maximum_candidates);
    if (request.caller_supplied_directories.size() > count) {
        state.inputs_within_limit = false;
        add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::resource_limit,
                            "recovery scan stopped at its caller-directory limit");
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& directory = request.caller_supplied_directories[index];
        if (directory.empty()) continue;
        const auto discovered = inspect_caller_recovery_directory(directory, state);
        for (const auto& entry : discovered) {
            if (!add_recovery_spec(state, entry.path, entry.kind)) {
                state.inputs_within_limit = false;
                break;
            }
            if (entry.kind == native_project_recovery_candidate_kind_t::caller_supplied_entry)
                state.completed_recovery_entries.push_back(entry.path);
        }
        if (!state.inputs_within_limit || state.specs.size() >= state.maximum_candidates) break;
    }
}

void discover_recovery_temporaries(const std::filesystem::path& destination,
                                   native_project_recovery_candidate_kind_t kind,
                                   recovery_collection_state_t& state) {
    if (destination.empty() || state.specs.size() >= state.maximum_candidates) return;
    const auto directory = destination.has_parent_path() ? destination.parent_path()
                                                          : std::filesystem::path{"."};
    std::error_code error;
    std::filesystem::directory_iterator iterator{directory, error};
    if (error) {
        add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not inspect recovery directory: " + error.message(), directory);
        return;
    }
    const auto prefix = temporary_prefix(destination);
    std::size_t examined{};
    for (const std::filesystem::directory_iterator end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                                native_project_storage_diagnostic_code_t::read_failure,
                                "could not continue recovery directory scan: " + error.message(),
                                directory);
            break;
        }
        if (++examined > state.maximum_directory_entries) {
            add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                                native_project_storage_diagnostic_code_t::resource_limit,
                                "recovery directory scan reached its entry limit", directory);
            break;
        }
        if (!path_to_utf8(iterator->path().filename()).starts_with(prefix)) continue;
        std::error_code type_error;
        if (!iterator->is_regular_file(type_error) || type_error) continue;
        if (!add_recovery_spec(state, iterator->path(), kind)) break;
    }
}

void collect_stale_recovery_temporaries(const native_project_recovery_scan_request_t& request,
                                        recovery_collection_state_t& state) {
    if (!request.discover_stale_temporaries || !state.inputs_within_limit ||
        request.primary_path.empty())
        return;
    discover_recovery_temporaries(
        request.primary_path, native_project_recovery_candidate_kind_t::stale_primary_temporary,
        state);
    const auto backup_path = native_project_adjacent_backup_path(request.primary_path);
    if (state.specs.size() >= state.maximum_candidates) {
        add_scan_diagnostic(state.diagnostics, state.maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::resource_limit,
                            "recovery scan skipped temporary discovery at its candidate limit",
                            backup_path);
    } else {
        discover_recovery_temporaries(
            backup_path, native_project_recovery_candidate_kind_t::stale_recovery_temporary,
            state);
    }
    for (const auto& path : state.completed_recovery_entries) {
        if (state.specs.size() >= state.maximum_candidates) break;
        discover_recovery_temporaries(
            path, native_project_recovery_candidate_kind_t::stale_recovery_temporary, state);
    }
}

auto collect_recovery_candidates(const native_project_recovery_scan_request_t& request,
                                 recovery_collection_state_t& state) -> bool {
    collect_recovery_primary_paths(request, state);
    collect_caller_recovery_entries(request, state);
    collect_caller_recovery_directories(request, state);
    collect_stale_recovery_temporaries(request, state);
    return state.inputs_within_limit;
}

auto read_recovery_candidate(const recovery_candidate_spec_t& spec,
                             std::size_t maximum_file_bytes,
                             std::size_t maximum_total_bytes, std::size_t& total_bytes_read,
                             native_project_recovery_candidate_t& candidate,
                             std::vector<native_project_storage_diagnostic_t>& diagnostics,
                             std::size_t maximum_diagnostics) -> std::optional<std::string> {
    std::error_code error;
    const auto exists = std::filesystem::exists(spec.path, error);
    if (error) {
        candidate.status = native_project_recovery_candidate_status_t::unreadable;
        add_scan_diagnostic(diagnostics, maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not inspect recovery candidate: " + error.message(), spec.path,
                            diagnostic_severity_t::error);
        return std::nullopt;
    }
    if (!exists) {
        candidate.status = native_project_recovery_candidate_status_t::missing;
        return std::nullopt;
    }
    const auto file_size = std::filesystem::file_size(spec.path, error);
    if (error) {
        candidate.status = native_project_recovery_candidate_status_t::unreadable;
        add_scan_diagnostic(diagnostics, maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not determine recovery candidate size: " + error.message(),
                            spec.path, diagnostic_severity_t::error);
        return std::nullopt;
    }
    if (file_size > maximum_file_bytes || file_size > maximum_total_bytes - total_bytes_read) {
        candidate.status = native_project_recovery_candidate_status_t::resource_limited;
        add_scan_diagnostic(diagnostics, maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::resource_limit,
                            "recovery candidate exceeds the scan byte budget", spec.path,
                            diagnostic_severity_t::error);
        return std::nullopt;
    }
    const auto byte_count = static_cast<std::size_t>(file_size);
    std::ifstream stream(spec.path, std::ios::binary);
    if (!stream) {
        candidate.status = native_project_recovery_candidate_status_t::unreadable;
        add_scan_diagnostic(diagnostics, maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not open recovery candidate", spec.path,
                            diagnostic_severity_t::error);
        return std::nullopt;
    }
    std::string bytes(byte_count, '\0');
    if (byte_count != 0) stream.read(bytes.data(), static_cast<std::streamsize>(byte_count));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != byte_count) {
        candidate.status = native_project_recovery_candidate_status_t::unreadable;
        add_scan_diagnostic(diagnostics, maximum_diagnostics,
                            native_project_storage_diagnostic_code_t::read_failure,
                            "could not read the complete recovery candidate", spec.path,
                            diagnostic_severity_t::error);
        return std::nullopt;
    }
    total_bytes_read += byte_count;
    candidate.scanned_size_bytes = file_size;
    candidate.scanned_sha256 = sha256(bytes);
    return bytes;
}

void parse_recovery_candidate(native_project_recovery_candidate_t& candidate,
                              const recovery_candidate_spec_t& spec, std::string_view bytes,
                              native_project_limits_t limits,
                              std::vector<native_project_storage_diagnostic_t>& diagnostics,
                              std::size_t maximum_diagnostics) {
    std::vector<native_project_storage_diagnostic_t> parse_diagnostics;
    const bool primary_file =
        spec.kind == native_project_recovery_candidate_kind_t::primary ||
        spec.kind == native_project_recovery_candidate_kind_t::stale_primary_temporary;
    if (primary_file) {
        const auto parsed = parse_native_project(bytes, limits);
        if (parsed.value && parsed.ok()) {
            candidate.document = *parsed.value;
            candidate.version = native_project_document_version_t{
                .project_id = parsed.value->id, .revision = parsed.value->revision};
            candidate.status = native_project_recovery_candidate_status_t::valid;
        } else {
            const auto limited = std::ranges::any_of(parsed.diagnostics, [](const auto& item) {
                return item.code == native_project_diagnostic_code_t::resource_limit;
            });
            candidate.status = limited ? native_project_recovery_candidate_status_t::resource_limited
                                       : native_project_recovery_candidate_status_t::invalid;
            for (const auto& item : parsed.diagnostics) {
                if (parse_diagnostics.size() >= maximum_diagnostics) break;
                auto converted = as_error(item);
                converted.path = spec.path;
                parse_diagnostics.push_back(std::move(converted));
            }
        }
    } else {
        const auto parsed = parse_recovery_impl(bytes, limits, parse_diagnostics, spec.path);
        if (parsed) {
            candidate.status = native_project_recovery_candidate_status_t::valid;
            candidate.document = parsed->snapshot.song;
            candidate.version = native_project_document_version_t{
                .project_id = parsed->snapshot.song.id,
                .revision = parsed->snapshot.song.revision};
            candidate.recovery_metadata = parsed->snapshot.metadata;
        } else {
            const auto limited = std::ranges::any_of(parse_diagnostics, [](const auto& item) {
                return item.code == native_project_storage_diagnostic_code_t::resource_limit;
            });
            candidate.status = limited ? native_project_recovery_candidate_status_t::resource_limited
                                       : native_project_recovery_candidate_status_t::invalid;
        }
    }
    for (auto& item : parse_diagnostics)
        add_scan_diagnostic(diagnostics, maximum_diagnostics, item.code,
                            std::move(item.message), spec.path, item.severity);
}

void scan_recovery_candidate(const recovery_candidate_spec_t& spec,
                             native_project_recovery_scan_options_t options,
                             std::size_t maximum_file_bytes, std::size_t maximum_total_bytes,
                             std::size_t& total_bytes_read,
                             std::vector<native_project_storage_diagnostic_t>& diagnostics,
                             std::size_t maximum_diagnostics,
                             std::vector<native_project_recovery_candidate_t>& candidates) {
    native_project_recovery_candidate_t candidate{};
    candidate.path = spec.path;
    candidate.kind = spec.kind;
    const auto bytes = read_recovery_candidate(spec, maximum_file_bytes, maximum_total_bytes,
                                               total_bytes_read, candidate, diagnostics,
                                               maximum_diagnostics);
    if (bytes) {
        parse_recovery_candidate(candidate, spec, *bytes, options.codec_limits, diagnostics,
                                 maximum_diagnostics);
        if (is_recovery_kind(candidate.kind) &&
            candidate.status == native_project_recovery_candidate_status_t::invalid)
            candidate.quarantine =
                native_project_recovery_quarantine_t::recommend_quarantine_corrupt;
    }
    candidates.push_back(std::move(candidate));
}

auto valid_primary_version(const std::vector<native_project_recovery_candidate_t>& candidates)
    -> std::optional<native_project_document_version_t> {
    for (const auto& candidate : candidates) {
        if (candidate.kind == native_project_recovery_candidate_kind_t::primary &&
            candidate.status == native_project_recovery_candidate_status_t::valid)
            return candidate.version;
    }
    return std::nullopt;
}

void recommend_recovery_actions(
    std::vector<native_project_recovery_candidate_t>& candidates,
    const std::optional<native_project_document_version_t>& primary_version) {
    for (auto& candidate : candidates) {
        if (candidate.kind == native_project_recovery_candidate_kind_t::primary ||
            candidate.status != native_project_recovery_candidate_status_t::valid)
            continue;
        candidate.available_actions = {native_project_recovery_action_t::inspect_read_only,
                                       native_project_recovery_action_t::recover_into_session,
                                       native_project_recovery_action_t::save_as_new_project,
                                       native_project_recovery_action_t::discard};
        if (!primary_version) {
            candidate.comparison = native_project_recovery_comparison_t::no_valid_primary;
            candidate.recommended_action = native_project_recovery_action_t::recover_into_session;
            continue;
        }
        candidate.comparison = compare_versions(*candidate.version, *primary_version);
        switch (candidate.comparison) {
        case native_project_recovery_comparison_t::newer_revision:
        case native_project_recovery_comparison_t::different_project:
            candidate.recommended_action = native_project_recovery_action_t::inspect_read_only;
            break;
        case native_project_recovery_comparison_t::older_revision:
        case native_project_recovery_comparison_t::equal_revision:
            candidate.recommended_action = native_project_recovery_action_t::discard;
            break;
        case native_project_recovery_comparison_t::not_comparable:
        case native_project_recovery_comparison_t::no_valid_primary:
            break;
        }
    }
}

void mark_superseded_temporaries(
    std::vector<native_project_recovery_candidate_t>& candidates) {
    for (auto& temporary : candidates) {
        if ((temporary.kind != native_project_recovery_candidate_kind_t::stale_primary_temporary &&
             temporary.kind != native_project_recovery_candidate_kind_t::stale_recovery_temporary) ||
            temporary.status != native_project_recovery_candidate_status_t::valid ||
            !temporary.version)
            continue;
        const auto superseded = std::ranges::any_of(candidates, [&](const auto& completed) {
            const bool completed_file =
                completed.kind == native_project_recovery_candidate_kind_t::primary ||
                completed.kind == native_project_recovery_candidate_kind_t::adjacent_backup ||
                completed.kind == native_project_recovery_candidate_kind_t::caller_supplied_entry;
            return completed_file &&
                   completed.status == native_project_recovery_candidate_status_t::valid &&
                   completed.version &&
                   completed.version->project_id == temporary.version->project_id &&
                   completed.version->revision >= temporary.version->revision;
        });
        if (superseded)
            temporary.quarantine =
                native_project_recovery_quarantine_t::recommend_quarantine_superseded_temporary;
    }
}

void add_quarantine_actions(std::vector<native_project_recovery_candidate_t>& candidates) {
    for (auto& candidate : candidates) {
        if (candidate.quarantine == native_project_recovery_quarantine_t::retain ||
            std::ranges::find(candidate.available_actions,
                              native_project_recovery_action_t::quarantine) !=
                candidate.available_actions.end())
            continue;
        candidate.available_actions.push_back(native_project_recovery_action_t::quarantine);
    }
}

auto choose_preferred_recovery(
    const std::vector<native_project_recovery_candidate_t>& candidates,
    const std::optional<native_project_document_version_t>& primary_version)
    -> std::optional<std::size_t> {
    std::vector<std::size_t> eligible;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        if (candidate.kind == native_project_recovery_candidate_kind_t::primary ||
            candidate.status != native_project_recovery_candidate_status_t::valid ||
            !candidate.version)
            continue;
        if (!primary_version ||
            candidate.comparison == native_project_recovery_comparison_t::newer_revision)
            eligible.push_back(index);
    }
    if (eligible.empty()) return std::nullopt;
    const auto project_id = candidates[eligible.front()].version->project_id;
    if (!std::ranges::all_of(eligible, [&](const auto index) {
            return candidates[index].version->project_id == project_id;
        }))
        return std::nullopt;
    auto best = eligible.front();
    for (const auto index : eligible) {
        if (recovery_candidate_precedes(candidates[index], candidates[best])) best = index;
    }
    return best;
}

void finalize_recovery_candidates(native_project_recovery_scan_outcome_t& result) {
    const auto primary_version = valid_primary_version(result.value.candidates);
    recommend_recovery_actions(result.value.candidates, primary_version);
    mark_superseded_temporaries(result.value.candidates);
    add_quarantine_actions(result.value.candidates);
    result.value.preferred_recovery_candidate =
        choose_preferred_recovery(result.value.candidates, primary_version);
}

} // namespace

auto scan_native_project_recovery_candidates(const native_project_recovery_scan_request_t& request,
                                             native_project_recovery_scan_options_t options)
    -> native_project_recovery_scan_outcome_t {
    native_project_recovery_scan_outcome_t result{};
    const auto maximum_candidates =
        std::min(options.maximum_candidates, absolute_maximum_scan_candidates);
    const auto maximum_file_bytes =
        std::min(options.maximum_candidate_file_bytes, absolute_maximum_scan_file_bytes);
    const auto maximum_total_bytes =
        std::min(options.maximum_total_bytes, absolute_maximum_scan_total_bytes);
    const auto maximum_diagnostics =
        std::min(options.maximum_diagnostics, absolute_maximum_scan_diagnostics);

    const auto maximum_directory_entries =
        std::min(std::max<std::size_t>(maximum_candidates * 16, 64),
                 absolute_maximum_scan_directory_entries);
    recovery_collection_state_t collection{result.diagnostics,
                                           maximum_candidates,
                                           maximum_diagnostics,
                                           maximum_directory_entries};
    collection.specs.reserve(std::min(maximum_candidates, std::size_t{16}));
    collection.completed_recovery_entries.reserve(std::min(maximum_candidates, std::size_t{16}));
    (void)collect_recovery_candidates(request, collection);

    std::size_t total_bytes_read{};
    result.value.candidates.reserve(collection.specs.size());
    for (const auto& spec : collection.specs) {
        scan_recovery_candidate(spec, options, maximum_file_bytes, maximum_total_bytes,
                                total_bytes_read, result.diagnostics, maximum_diagnostics,
                                result.value.candidates);
    }

    finalize_recovery_candidates(result);
    return result;
}

namespace {

auto publish_native_project_create_only(const std::filesystem::path& destination,
                                        const song_document_t& song, native_project_limits_t limits)
    -> native_project_storage_outcome_t {
    native_project_storage_outcome_t result{};
    if (destination.empty() || destination.filename().empty()) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::invalid_destination,
                       "destination must name a file", destination);
        return result;
    }
    auto serialized = write_native_project(song, limits);
    if (!serialized.value.has_value() || !serialized.ok()) {
        copy_codec_diagnostics(result.diagnostics, serialized.diagnostics);
        if (result.diagnostics.empty()) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::serialization_failure,
                           "native-project serialization did not produce output", destination);
        }
        return result;
    }
    const expected_document_t expected{.id = song.id, .revision = song.revision};
    if (!validate_native_payload(*serialized.value, expected, limits, result.diagnostics,
                                 destination)) {
        return result;
    }
    native_project_storage_options_t options{.codec_limits = limits};
    return publish_prevalidated_bytes(
        destination, *serialized.value, options,
        [expected, limits](std::string_view reopened, std::vector<native_project_storage_diagnostic_t>& diagnostics, const std::filesystem::path& path) {
            return validate_native_payload(reopened, expected, limits, diagnostics, path);
        },
        false);
}

auto is_recovery_action_stage_allowed(const native_project_recovery_action_options_t& options,
                                   native_project_recovery_action_stage_t stage,
                                   const std::filesystem::path& path) noexcept -> bool {
    if (!options.allow_stage) {
        return true;
    }
    try {
        return options.allow_stage(stage, path);
    } catch (...) {
        return false;
    }
}

void add_action_error(native_project_recovery_action_outcome_t& result,
                      native_project_storage_diagnostic_code_t code, std::string message,
                      const std::filesystem::path& path = {},
                      diagnostic_severity_t severity = diagnostic_severity_t::error) {
    add_diagnostic(result.diagnostics, code, std::move(message), path, severity);
}

auto read_scanned_artifact(const native_project_recovery_candidate_t& candidate,
                           std::size_t maximum_file_bytes,
                           native_project_recovery_action_outcome_t& result)
    -> std::optional<std::string> {
    if (!candidate.scanned_size_bytes.has_value() || candidate.scanned_sha256.size() != 64) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                         "candidate has no complete scanner identity", candidate.path);
        return std::nullopt;
    }
    if (*candidate.scanned_size_bytes > maximum_file_bytes ||
        *candidate.scanned_size_bytes > absolute_maximum_scan_file_bytes) {
        add_action_error(result, native_project_storage_diagnostic_code_t::resource_limit,
                         "recovery candidate exceeds the action byte limit", candidate.path);
        return std::nullopt;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(candidate.path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        add_action_error(
            result, native_project_storage_diagnostic_code_t::recovery_candidate_changed,
            "scanned recovery artifact is missing or is no longer a regular file", candidate.path);
        return std::nullopt;
    }
    const auto current_size = std::filesystem::file_size(candidate.path, error);
    if (error || current_size != *candidate.scanned_size_bytes) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_candidate_changed,
                         "scanned recovery artifact size changed", candidate.path);
        return std::nullopt;
    }
    const auto byte_count = static_cast<std::size_t>(current_size);
    std::ifstream stream(candidate.path, std::ios::binary);
    if (!stream) {
        add_action_error(result, native_project_storage_diagnostic_code_t::read_failure,
                         "could not reopen scanned recovery artifact", candidate.path);
        return std::nullopt;
    }
    std::string bytes(byte_count, '\0');
    if (byte_count != 0) {
        stream.read(bytes.data(), static_cast<std::streamsize>(byte_count));
    }
    if (!stream || static_cast<std::size_t>(stream.gcount()) != byte_count) {
        add_action_error(result, native_project_storage_diagnostic_code_t::read_failure,
                         "could not read the complete scanned recovery artifact", candidate.path);
        return std::nullopt;
    }
    error.clear();
    const auto final_size = std::filesystem::file_size(candidate.path, error);
    if (error || final_size != current_size || sha256(bytes) != candidate.scanned_sha256) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_candidate_changed,
                         "scanned recovery artifact was replaced or modified", candidate.path);
        return std::nullopt;
    }
    return bytes;
}

auto parse_scanned_artifact(const native_project_recovery_candidate_t& candidate,
                            std::string_view bytes, native_project_limits_t limits,
                            native_project_recovery_action_outcome_t& result)
    -> std::optional<song_document_t> {
    const auto raw_native =
        candidate.kind == native_project_recovery_candidate_kind_t::primary ||
        candidate.kind == native_project_recovery_candidate_kind_t::stale_primary_temporary;
    if (raw_native) {
        const auto parsed = parse_native_project(bytes, limits);
        if (parsed.value.has_value() && parsed.ok()) {
            return parsed.value;
        }
        add_action_error(result, native_project_storage_diagnostic_code_t::validation_failure,
                         "scanned native project no longer validates", candidate.path);
        return std::nullopt;
    }
    std::vector<native_project_storage_diagnostic_t> diagnostics;
    const auto parsed = parse_recovery_impl(bytes, limits, diagnostics, candidate.path);
    if (parsed.has_value()) {
        return parsed->snapshot.song;
    }
    if (diagnostics.empty()) {
        add_action_error(result, native_project_storage_diagnostic_code_t::validation_failure,
                         "scanned recovery envelope no longer validates", candidate.path);
    } else {
        for (auto& diagnostic : diagnostics) {
            if (result.diagnostics.size() == maximum_diagnostics) {
                break;
            }
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }
    return std::nullopt;
}

auto is_same_path_for_action(const std::filesystem::path& left, const std::filesystem::path& right)
    -> bool {
    std::error_code left_error;
    std::error_code right_error;
    auto left_normalized = std::filesystem::weakly_canonical(left, left_error);
    auto right_normalized = std::filesystem::weakly_canonical(right, right_error);
    if (left_error) {
        left_normalized = std::filesystem::absolute(left, left_error).lexically_normal();
    }
    if (right_error) {
        right_normalized = std::filesystem::absolute(right, right_error).lexically_normal();
    }
    return left_normalized == right_normalized ||
           left.lexically_normal() == right.lexically_normal();
}

auto create_quarantine_path(const std::filesystem::path& source) -> std::filesystem::path {
    static std::atomic<std::uint64_t> sequence{1};
    const auto parent =
        source.has_parent_path() ? source.parent_path() : std::filesystem::path{"."};
    auto name = source.filename();
    name += ".wormedit-quarantine-" + std::to_string(process_id()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    return parent / name;
}

auto validated_scanned_document(const native_project_recovery_candidate_t& candidate,
                                std::string_view bytes, native_project_limits_t limits,
                                native_project_recovery_action_outcome_t& result)
    -> std::optional<song_document_t> {
    if (candidate.status != native_project_recovery_candidate_status_t::valid) {
        return std::nullopt;
    }
    auto document = parse_scanned_artifact(candidate, bytes, limits, result);
    if (!document) {
        return std::nullopt;
    }
    const native_project_document_version_t version{.project_id = document->id,
                                                    .revision = document->revision};
    if (!candidate.version || *candidate.version != version) {
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                         "candidate project identity or revision differs from its scan result",
                         candidate.path);
        return std::nullopt;
    }
    return document;
}

auto scanned_primary(std::span<const native_project_recovery_candidate_t> scan)
    -> const native_project_recovery_candidate_t* {
    const auto primary = std::ranges::find_if(scan, [](const auto& entry) {
        return entry.kind == native_project_recovery_candidate_kind_t::primary;
    });
    return primary == scan.end() ? nullptr : &*primary;
}

auto verify_primary_baseline(const native_project_recovery_candidate_t& primary,
                             const native_project_recovery_action_options_t& options,
                             native_project_recovery_action_outcome_t& result) -> bool {
    if (primary.status == native_project_recovery_candidate_status_t::missing) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(primary.path, error);
        if ((!error || error == std::errc::no_such_file_or_directory) &&
            !std::filesystem::exists(status)) {
            return true;
        }
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_candidate_changed,
                         "primary appeared after the recovery scan", primary.path);
        return false;
    }
    const auto primary_bytes =
        read_scanned_artifact(primary, options.maximum_candidate_file_bytes, result);
    if (!primary_bytes) {
        return false;
    }
    if (primary.status != native_project_recovery_candidate_status_t::valid) {
        return true;
    }
    auto primary_document =
        parse_scanned_artifact(primary, *primary_bytes, options.codec_limits, result);
    if (!primary_document || !primary.version ||
        *primary.version != native_project_document_version_t{
                                .project_id = primary_document->id,
                                .revision = primary_document->revision}) {
        if (result.ok()) {
            add_action_error(result,
                             native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                             "primary identity or revision differs from its scan result",
                             primary.path);
        }
        return false;
    }
    return true;
}

void save_recovery_as_new_project(std::span<const native_project_recovery_candidate_t> scan,
                                  const native_project_recovery_candidate_t& candidate,
                                  const native_project_recovery_action_options_t& options,
                                  native_project_recovery_action_outcome_t& result) {
    const auto& destination = options.save_as_destination;
    if (destination.empty() || destination.filename().empty()) {
        add_action_error(result, native_project_storage_diagnostic_code_t::invalid_destination,
                         "save-as destination must name a file", destination);
        return;
    }
    for (const auto& entry : scan) {
        if (entry.kind == native_project_recovery_candidate_kind_t::primary &&
            is_same_path_for_action(destination, entry.path)) {
            add_action_error(result, native_project_storage_diagnostic_code_t::invalid_destination,
                             "save-as cannot target the primary project", destination);
            return;
        }
    }
    if (is_same_path_for_action(destination, candidate.path)) {
        add_action_error(result, native_project_storage_diagnostic_code_t::invalid_destination,
                         "save-as cannot target the scanned recovery artifact", destination);
        return;
    }
    std::error_code error;
    const auto destination_status = std::filesystem::symlink_status(destination, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        add_action_error(result, native_project_storage_diagnostic_code_t::invalid_destination,
                         "could not inspect save-as destination: " + error.message(), destination);
        return;
    }
    if (!error && std::filesystem::exists(destination_status)) {
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_target_exists,
                         "save-as destination already exists", destination);
        return;
    }
    if (!is_recovery_action_stage_allowed(options,
                                       native_project_recovery_action_stage_t::before_save_as_publish,
                                       destination)) {
        add_action_error(result, native_project_storage_diagnostic_code_t::write_failure,
                         "injected save-as publication failure", destination);
        return;
    }
    const auto candidate_bytes =
        read_scanned_artifact(candidate, options.maximum_candidate_file_bytes, result);
    if (!candidate_bytes) {
        return;
    }
    auto document = validated_scanned_document(candidate, *candidate_bytes, options.codec_limits,
                                               result);
    if (!document) {
        return;
    }
    const auto publication =
        publish_native_project_create_only(destination, *document, options.codec_limits);
    result.diagnostics = publication.diagnostics;
    if (!publication.value || !publication.ok()) {
        for (auto& diagnostic : result.diagnostics) {
            if (diagnostic.code == native_project_storage_diagnostic_code_t::replacement_failure) {
                diagnostic.code = native_project_storage_diagnostic_code_t::recovery_target_exists;
                diagnostic.message =
                    "save-as could not claim a destination that appeared concurrently";
            }
        }
        return;
    }
    result.value.document = std::move(document);
    result.value.version = candidate.version;
    result.value.applied = true;
}

void quarantine_scanned_recovery(std::span<const native_project_recovery_candidate_t> scan,
                                 const native_project_recovery_candidate_t& candidate,
                                 const native_project_recovery_action_options_t& options,
                                 native_project_recovery_action_outcome_t& result) {
    if (!is_recovery_action_stage_allowed(options,
                                       native_project_recovery_action_stage_t::before_mutation,
                                       candidate.path)) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_quarantine_failure,
                         "injected recovery quarantine failure", candidate.path);
        return;
    }
    const auto candidate_bytes =
        read_scanned_artifact(candidate, options.maximum_candidate_file_bytes, result);
    if (!candidate_bytes) {
        return;
    }
    if (const auto* primary = scanned_primary(scan);
        primary != nullptr && !verify_primary_baseline(*primary, options, result)) {
        return;
    }
    const auto destination = create_quarantine_path(candidate.path);
    std::string move_failure;
    if (!move_no_replace(candidate.path, destination, move_failure)) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_quarantine_failure,
                         std::move(move_failure), candidate.path);
        return;
    }
    result.value.quarantined_path = destination;
    if (!is_recovery_action_stage_allowed(
            options, native_project_recovery_action_stage_t::after_quarantine_move, destination)) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_quarantine_failure,
                         "injected post-move quarantine verification failure", destination);
    } else {
        auto moved_candidate = candidate;
        moved_candidate.path = destination;
        const auto moved_bytes =
            read_scanned_artifact(moved_candidate, options.maximum_candidate_file_bytes, result);
        if (moved_bytes) {
            result.value.applied = true;
        }
    }
    if (!result.value.applied) {
        std::string restore_failure;
        if (move_no_replace(destination, candidate.path, restore_failure)) {
            result.value.quarantined_path.reset();
        } else {
            add_action_error(result,
                             native_project_storage_diagnostic_code_t::recovery_quarantine_failure,
                             "could not restore the changed artifact after quarantine verification: " +
                                 restore_failure,
                             destination);
        }
        return;
    }
    std::string directory_failure;
    if (!sync_directory(destination, directory_failure)) {
        add_action_error(result, native_project_storage_diagnostic_code_t::write_failure,
                         "recovery artifact was moved, but " + directory_failure, destination,
                         diagnostic_severity_t::warning);
    }
}

void recover_scanned_document(std::span<const native_project_recovery_candidate_t> scan,
                              const native_project_recovery_candidate_t& candidate,
                              const native_project_recovery_action_options_t& options,
                              std::optional<song_document_t> document,
                              native_project_recovery_action_outcome_t& result) {
    const auto* primary = scanned_primary(scan);
    if (primary == nullptr && options.expected_session.version) {
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                         "a nonempty session cannot recover from a scan with no primary baseline",
                         candidate.path);
        return;
    }
    if (primary != nullptr && !verify_primary_baseline(*primary, options, result)) {
        return;
    }
    const auto primary_version =
        primary != nullptr && primary->status == native_project_recovery_candidate_status_t::valid
            ? primary->version
            : std::optional<native_project_document_version_t>{};
    if (options.expected_session.version != primary_version) {
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                         "current session no longer matches the scanned primary revision",
                         primary != nullptr ? primary->path : candidate.path);
        return;
    }
    if (options.expected_session.version &&
        (candidate.version->project_id != options.expected_session.version->project_id ||
         candidate.version->revision < options.expected_session.version->revision)) {
        add_action_error(result, native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                         "recovery candidate belongs to another project or is older than the session",
                         candidate.path);
        return;
    }
    result.value.document = std::move(document);
    result.value.version = candidate.version;
    result.value.applied = true;
}

} // namespace

auto create_native_project_file(const std::filesystem::path& destination,
                                   const song_document_t& song,
                                   native_project_storage_options_t options)
    -> native_project_storage_outcome_t {
    native_project_storage_outcome_t result{};
    if (!is_stage_allowed(options, native_project_storage_stage_t::serialize, destination)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::serialization_failure,
                       stage_failure(native_project_storage_stage_t::serialize), destination);
        return result;
    }
    auto serialized = write_native_project(song, options.codec_limits);
    if (!serialized.value.has_value() || !serialized.ok()) {
        copy_codec_diagnostics(result.diagnostics, serialized.diagnostics);
        if (result.diagnostics.empty()) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::serialization_failure,
                           "native-project serialization did not produce output", destination);
        }
        return result;
    }
    if (!is_stage_allowed(options, native_project_storage_stage_t::preflight_validate, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::preflight_validate), destination);
        return result;
    }
    if (!validate_native_payload(*serialized.value, {.id = song.id, .revision = song.revision},
                                 options.codec_limits, result.diagnostics, destination)) {
        return result;
    }
    const expected_document_t expected{.id = song.id, .revision = song.revision};
    return publish_prevalidated_bytes(
        destination, *serialized.value, options,
        [expected, limits = options.codec_limits](std::string_view reopened, std::vector<native_project_storage_diagnostic_t>& diagnostics,
                                                  const std::filesystem::path& path) {
            return validate_native_payload(reopened, expected, limits, diagnostics, path);
        });
}

auto create_native_bytes_file(const std::filesystem::path& destination, std::string_view bytes,
                                native_project_storage_options_t options)
    -> native_project_storage_outcome_t {
    const std::string expected{bytes};
    return publish_prevalidated_bytes(
        destination, bytes, options,
        [expected](std::string_view reopened, std::vector<native_project_storage_diagnostic_t>& diagnostics, const std::filesystem::path& path) {
            if (reopened == expected) return true;
            add_diagnostic(diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
                           "reopened bytes do not match the bytes that were written", path);
            return false;
        });
}

auto create_native_project_recovery_file(const std::filesystem::path& destination,
                                            const song_document_t& song,
                                            native_project_recovery_metadata_t metadata,
                                            native_project_storage_options_t options)
    -> native_project_storage_outcome_t {
    native_project_storage_outcome_t result{};
    if (metadata.last_explicit_save_revision.has_value() &&
        *metadata.last_explicit_save_revision > song.revision) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            "last explicit-save revision cannot be newer than the recovery snapshot", destination);
        return result;
    }
    if (!is_stage_allowed(options, native_project_storage_stage_t::serialize, destination)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::serialization_failure,
                       stage_failure(native_project_storage_stage_t::serialize), destination);
        return result;
    }
    auto serialized = write_native_project(song, options.codec_limits);
    if (!serialized.value.has_value() || !serialized.ok()) {
        copy_codec_diagnostics(result.diagnostics, serialized.diagnostics);
        if (result.diagnostics.empty()) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::serialization_failure,
                           "native-project serialization did not produce output", destination);
        }
        return result;
    }
    if (!is_stage_allowed(options, native_project_storage_stage_t::preflight_validate, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::preflight_validate), destination);
        return result;
    }
    const expected_document_t expected{.id = song.id, .revision = song.revision};
    if (!validate_native_payload(*serialized.value, expected, options.codec_limits,
                                 result.diagnostics, destination)) {
        return result;
    }
    auto envelope =
        recovery_envelope(song, *serialized.value, metadata, options, result.diagnostics);
    if (!envelope.has_value()) {
        return result;
    }
    const auto preflight =
        parse_recovery_impl(*envelope, options.codec_limits, result.diagnostics, destination);
    if (!preflight.has_value()) {
        return result;
    }
    return publish_prevalidated_bytes(
        destination, *envelope, options,
        [limits = options.codec_limits, expected](std::string_view reopened, std::vector<native_project_storage_diagnostic_t>& diagnostics,
                                                  const std::filesystem::path& path) {
            const auto parsed = parse_recovery_impl(reopened, limits, diagnostics, path);
            return parsed.has_value() && is_document_current(parsed->snapshot.song, expected);
        });
}

auto perform_native_project_recovery_action(std::span<const native_project_recovery_candidate_t> scan,
                                          std::size_t candidate_index,
                                          native_project_recovery_action_t action,
                                          native_project_recovery_action_options_t options)
    -> native_project_recovery_action_outcome_t {
    native_project_recovery_action_outcome_t result{};
    if (candidate_index >= scan.size()) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                         "recovery candidate index is outside the completed scan");
        return result;
    }
    const auto& candidate = scan[candidate_index];
    if (std::ranges::find(candidate.available_actions, action) ==
        candidate.available_actions.end()) {
        add_action_error(
            result, native_project_storage_diagnostic_code_t::recovery_action_unavailable,
            "requested recovery action is not available for this candidate", candidate.path);
        return result;
    }
    if (candidate.status == native_project_recovery_candidate_status_t::missing ||
        candidate.status == native_project_recovery_candidate_status_t::unreadable ||
        candidate.status == native_project_recovery_candidate_status_t::resource_limited) {
        add_action_error(
            result, native_project_storage_diagnostic_code_t::recovery_action_unavailable,
            "candidate was not completely scanned and cannot be acted on", candidate.path);
        return result;
    }
    if (action == native_project_recovery_action_t::recover_into_session &&
        !options.expected_session.is_set) {
        add_action_error(result,
                         native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                         "recovering into a session requires its expected identity and revision",
                         candidate.path);
        return result;
    }

    const auto allowed = is_recovery_action_stage_allowed(
        options, native_project_recovery_action_stage_t::before_revalidate, candidate.path);
    if (!allowed) {
        add_action_error(result, native_project_storage_diagnostic_code_t::read_failure,
                         "injected recovery-candidate revalidation failure", candidate.path);
        return result;
    }
    auto candidate_bytes =
        read_scanned_artifact(candidate, options.maximum_candidate_file_bytes, result);
    if (!candidate_bytes.has_value()) {
        return result;
    }

    auto document = validated_scanned_document(candidate, *candidate_bytes, options.codec_limits,
                                               result);
    if (candidate.status == native_project_recovery_candidate_status_t::valid &&
        !document.has_value()) {
        return result;
    }

    if (action == native_project_recovery_action_t::inspect_read_only) {
        result.value.document = std::move(document);
        result.value.version = candidate.version;
        result.value.applied = true;
        return result;
    }

    if (action == native_project_recovery_action_t::recover_into_session) {
        recover_scanned_document(scan, candidate, options, std::move(document), result);
        return result;
    }

    if (action == native_project_recovery_action_t::save_as_new_project) {
        save_recovery_as_new_project(scan, candidate, options, result);
        return result;
    }

    if (action == native_project_recovery_action_t::discard ||
        action == native_project_recovery_action_t::quarantine) {
        quarantine_scanned_recovery(scan, candidate, options, result);
        return result;
    }

    add_action_error(result, native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                     "unsupported recovery action", candidate.path);
    return result;
}

native_project_autosave_coordinator_t::native_project_autosave_coordinator_t(
    clock_t::time_point session_started_at,
    std::optional<native_project_document_version_t> last_explicit_save,
    std::chrono::steady_clock::duration interval, std::uint64_t initial_session_sequence)
    : last_successful_autosave_at_(session_started_at), interval_(interval),
      last_explicit_save_(last_explicit_save), session_sequence_(initial_session_sequence),
      interval_valid_(interval > std::chrono::steady_clock::duration::zero()) {}

auto native_project_autosave_coordinator_t::set_interval(clock_t::duration interval) noexcept -> bool {
    if (interval <= clock_t::duration::zero()) return false;
    interval_ = interval;
    interval_valid_ = true;
    return true;
}

void native_project_autosave_coordinator_t::mark_explicit_save(
    native_project_document_version_t saved_version) noexcept {
    last_explicit_save_ = saved_version;
}

auto native_project_autosave_coordinator_t::is_due(const song_document_t& song,
                                                   clock_t::time_point now) const noexcept -> bool {
    const auto version = to_version(song);
    return interval_valid_ &&
           !(last_explicit_save_.has_value() && *last_explicit_save_ == version) &&
           !(last_autosaved_.has_value() && *last_autosaved_ == version) &&
           now >= last_successful_autosave_at_ && now - last_successful_autosave_at_ >= interval_;
}

auto native_project_autosave_coordinator_t::create_autosave(
    const song_document_t& song, clock_t::time_point now,
    const std::filesystem::path& caller_supplied_backup_path,
    std::filesystem::path original_source_path, std::filesystem::path intended_project_path,
    native_project_storage_options_t options) -> native_project_autosave_outcome_t {
    native_project_autosave_outcome_t result{.value = {.revision = song.revision,
        .session_sequence = session_sequence_}, .diagnostics = {}};
    if (!interval_valid_) {
        result.value.status = native_project_autosave_status_t::failed;
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::invalid_interval,
                       "autosave interval must be positive", caller_supplied_backup_path);
        return result;
    }
    const auto version = to_version(song);
    if (!is_due(song, now)) {
        return result;
    }
    if (session_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        result.value.status = native_project_autosave_status_t::failed;
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::resource_limit,
                       "autosave session sequence is exhausted", caller_supplied_backup_path);
        return result;
    }
    native_project_recovery_metadata_t metadata{
        .original_source_path = std::move(original_source_path),
        .intended_project_path = std::move(intended_project_path),
        .last_explicit_save_revision =
            last_explicit_save_.has_value() && last_explicit_save_->project_id == song.id
                ? std::optional<std::uint64_t>{last_explicit_save_->revision}
                : std::nullopt,
        .utc_write_time = std::chrono::system_clock::now(),
        .session_sequence = session_sequence_ + 1,
    };
    const auto publication = create_native_project_recovery_file(
        caller_supplied_backup_path, song, std::move(metadata), std::move(options));
    result.diagnostics = publication.diagnostics;
    if (!publication.value || !publication.ok()) {
        result.value.status = native_project_autosave_status_t::failed;
        return result;
    }
    last_successful_autosave_at_ = now;
    last_autosaved_ = version;
    ++session_sequence_;
    result.value.session_sequence = session_sequence_;
    result.value.status = native_project_autosave_status_t::published;
    return result;
}

auto native_project_autosave_coordinator_t::last_autosaved_version() const noexcept
    -> std::optional<native_project_document_version_t> {
    return last_autosaved_;
}

auto native_project_autosave_coordinator_t::session_sequence() const noexcept -> std::uint64_t {
    return session_sequence_;
}

} // namespace wormedit

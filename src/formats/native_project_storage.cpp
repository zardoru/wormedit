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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace wormedit {
namespace {

using json = nlohmann::json;

constexpr std::string_view recovery_format = "wormedit-recovery";
constexpr std::uint64_t recovery_format_version = 1;
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
    const auto encoded = path.generic_u8string();
    return {encoded.begin(), encoded.end()};
}

auto path_from_utf8(std::string_view path) -> std::filesystem::path {
    std::u8string encoded;
    encoded.reserve(path.size());
    for (const auto byte : path) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{encoded};
}

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

auto stage_allowed(const native_project_storage_options_t& options,
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

auto matches(const song_document_t& song, expected_document_t expected) -> bool {
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
    if (!stage_allowed(options, native_project_storage_stage_t::checksum, {})) {
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
    if (!parsed.song.has_value() || parsed.has_errors()) {
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
    if (!matches(*parsed.song, expected)) {
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

    [[nodiscard]] auto limit_exceeded() const noexcept -> bool {
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
            sax.limit_exceeded() ? native_project_storage_diagnostic_code_t::resource_limit
                                 : native_project_storage_diagnostic_code_t::validation_failure,
            sax.limit_exceeded() ? "recovery envelope exceeds its node, depth, or string limit"
                                 : "recovery envelope JSON is malformed",
            path);
        return std::nullopt;
    }
    try {
        const auto root = json::parse(bytes);
        if (!root.is_object() || root.size() != 13 ||
            root.value("format", std::string{}) != recovery_format ||
            strict_unsigned(root.at("format_version")) != recovery_format_version ||
            strict_unsigned(root.at("schema_version")) != 1) {
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
        if (!parsed_song.song.has_value() || parsed_song.has_errors()) {
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
        if (!matches(*parsed_song.song, expected)) {
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
                .song = std::move(*parsed_song.song),
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
                         const std::filesystem::path& destination, std::string& failure) -> bool {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        failure = "atomic destination replacement failed with Windows error " +
                  std::to_string(GetLastError());
        return false;
    }
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
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

template <typename validator_t>
auto publish_prevalidated_bytes(const std::filesystem::path& destination, std::string_view bytes,
                                const native_project_storage_options_t& options,
                                validator_t&& validate_reopened)
    -> native_project_storage_result_t {
    native_project_storage_result_t result;
    if (destination.empty() || destination.filename().empty()) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::invalid_destination,
                       "destination must name a file", destination);
        return result;
    }

    static std::atomic<std::uint64_t> temporary_sequence{1};
    temporary_file_t temporary;
    bool created{};
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto sequence = temporary_sequence.fetch_add(1, std::memory_order_relaxed);
        temporary.path = make_temporary_path(destination, sequence);
        if (!stage_allowed(options, native_project_storage_stage_t::create_temporary,
                           temporary.path)) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::temporary_file_failure,
                           stage_failure(native_project_storage_stage_t::create_temporary),
                           destination);
            return result;
        }
        int error{};
        if (create_exclusive_file(temporary.path, temporary.handle, error)) {
            created = true;
            break;
        }
#ifdef _WIN32
        const auto collision = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
#else
        const auto collision = error == EEXIST;
#endif
        if (!collision) {
            add_diagnostic(result.diagnostics,
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
            return result;
        }
    }
    if (!created) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::temporary_file_failure,
                       "could not reserve a unique same-directory temporary file", destination);
        return result;
    }

    if (!stage_allowed(options, native_project_storage_stage_t::write_temporary, temporary.path)) {
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

    if (!stage_allowed(options, native_project_storage_stage_t::flush_temporary, temporary.path)) {
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

    if (!stage_allowed(options, native_project_storage_stage_t::close_temporary, temporary.path)) {
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

    if (!stage_allowed(options, native_project_storage_stage_t::reopen_temporary, temporary.path)) {
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

    if (!stage_allowed(options, native_project_storage_stage_t::validate_reopened,
                       temporary.path)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::validate_reopened), temporary.path);
        return result;
    }
    if (!validate_reopened(reopened_bytes, result.diagnostics, temporary.path)) {
        return result;
    }

    if (!stage_allowed(options, native_project_storage_stage_t::replace_destination, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::replacement_failure,
            stage_failure(native_project_storage_stage_t::replace_destination), destination);
        return result;
    }
    std::string replace_failure;
    if (!replace_destination(temporary.path, destination, replace_failure)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::replacement_failure,
                       std::move(replace_failure), destination);
        return result;
    }
    temporary.keep = true;
    result.published = true;

    std::string directory_failure;
    if (!stage_allowed(options, native_project_storage_stage_t::sync_destination_directory,
                       destination) ||
        !sync_directory(destination, directory_failure)) {
        if (directory_failure.empty()) {
            directory_failure =
                stage_failure(native_project_storage_stage_t::sync_destination_directory);
        }
        add_diagnostic(result.diagnostics, native_project_storage_diagnostic_code_t::write_failure,
                       "destination was replaced, but " + directory_failure, destination,
                       diagnostic_severity_t::warning);
    }
    return result;
}

} // namespace

auto native_project_storage_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto native_project_recovery_parse_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto native_project_adjacent_backup_path(const std::filesystem::path& primary)
    -> std::filesystem::path {
    auto filename = primary.filename();
    filename += ".bak";
    return primary.has_parent_path() ? primary.parent_path() / filename : filename;
}

auto parse_native_project_recovery_snapshot(std::string_view bytes, native_project_limits_t limits)
    -> native_project_recovery_parse_result_t {
    native_project_recovery_parse_result_t result;
    const auto parsed = parse_recovery_impl(bytes, limits, result.diagnostics);
    if (parsed.has_value()) {
        result.snapshot = parsed->snapshot;
    }
    return result;
}

auto publish_native_project_atomic(const std::filesystem::path& destination,
                                   const song_document_t& song,
                                   native_project_storage_options_t options)
    -> native_project_storage_result_t {
    native_project_storage_result_t result;
    if (!stage_allowed(options, native_project_storage_stage_t::serialize, destination)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::serialization_failure,
                       stage_failure(native_project_storage_stage_t::serialize), destination);
        return result;
    }
    auto serialized = write_native_project(song, options.codec_limits);
    if (!serialized.bytes.has_value() || serialized.has_errors()) {
        copy_codec_diagnostics(result.diagnostics, serialized.diagnostics);
        if (result.diagnostics.empty()) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::serialization_failure,
                           "native-project serialization did not produce output", destination);
        }
        return result;
    }
    if (!stage_allowed(options, native_project_storage_stage_t::preflight_validate, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::preflight_validate), destination);
        return result;
    }
    if (!validate_native_payload(*serialized.bytes, {.id = song.id, .revision = song.revision},
                                 options.codec_limits, result.diagnostics, destination)) {
        return result;
    }
    const expected_document_t expected{.id = song.id, .revision = song.revision};
    return publish_prevalidated_bytes(
        destination, *serialized.bytes, options,
        [expected, limits = options.codec_limits](std::string_view reopened, auto& diagnostics,
                                                  const auto& path) {
            return validate_native_payload(reopened, expected, limits, diagnostics, path);
        });
}

auto publish_native_project_recovery_atomic(const std::filesystem::path& destination,
                                            const song_document_t& song,
                                            native_project_recovery_metadata_t metadata,
                                            native_project_storage_options_t options)
    -> native_project_storage_result_t {
    native_project_storage_result_t result;
    if (metadata.last_explicit_save_revision.has_value() &&
        *metadata.last_explicit_save_revision > song.revision) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            "last explicit-save revision cannot be newer than the recovery snapshot", destination);
        return result;
    }
    if (!stage_allowed(options, native_project_storage_stage_t::serialize, destination)) {
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::serialization_failure,
                       stage_failure(native_project_storage_stage_t::serialize), destination);
        return result;
    }
    auto serialized = write_native_project(song, options.codec_limits);
    if (!serialized.bytes.has_value() || serialized.has_errors()) {
        copy_codec_diagnostics(result.diagnostics, serialized.diagnostics);
        if (result.diagnostics.empty()) {
            add_diagnostic(result.diagnostics,
                           native_project_storage_diagnostic_code_t::serialization_failure,
                           "native-project serialization did not produce output", destination);
        }
        return result;
    }
    if (!stage_allowed(options, native_project_storage_stage_t::preflight_validate, destination)) {
        add_diagnostic(
            result.diagnostics, native_project_storage_diagnostic_code_t::validation_failure,
            stage_failure(native_project_storage_stage_t::preflight_validate), destination);
        return result;
    }
    const expected_document_t expected{.id = song.id, .revision = song.revision};
    if (!validate_native_payload(*serialized.bytes, expected, options.codec_limits,
                                 result.diagnostics, destination)) {
        return result;
    }
    auto envelope =
        recovery_envelope(song, *serialized.bytes, metadata, options, result.diagnostics);
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
        [limits = options.codec_limits, expected](std::string_view reopened, auto& diagnostics,
                                                  const auto& path) {
            const auto parsed = parse_recovery_impl(reopened, limits, diagnostics, path);
            return parsed.has_value() && matches(parsed->snapshot.song, expected);
        });
}

native_project_autosave_coordinator_t::native_project_autosave_coordinator_t(
    clock_t::time_point session_started_at,
    std::optional<native_project_document_version_t> last_explicit_save,
    std::chrono::steady_clock::duration interval, std::uint64_t initial_session_sequence)
    : last_successful_autosave_at_(session_started_at), interval_(interval),
      last_explicit_save_(last_explicit_save), session_sequence_(initial_session_sequence),
      interval_valid_(interval > std::chrono::steady_clock::duration::zero()) {}

void native_project_autosave_coordinator_t::mark_explicit_save(
    native_project_document_version_t saved_version) noexcept {
    last_explicit_save_ = saved_version;
}

auto native_project_autosave_coordinator_t::maybe_autosave(
    const song_document_t& song, clock_t::time_point now,
    const std::filesystem::path& caller_supplied_backup_path,
    std::filesystem::path original_source_path, std::filesystem::path intended_project_path,
    native_project_storage_options_t options) -> native_project_autosave_result_t {
    native_project_autosave_result_t result{.revision = song.revision,
                                            .session_sequence = session_sequence_};
    if (!interval_valid_) {
        result.status = native_project_autosave_status_t::failed;
        add_diagnostic(result.diagnostics,
                       native_project_storage_diagnostic_code_t::invalid_interval,
                       "autosave interval must be positive", caller_supplied_backup_path);
        return result;
    }
    const auto version = to_version(song);
    if ((last_explicit_save_.has_value() && *last_explicit_save_ == version) ||
        (last_autosaved_.has_value() && *last_autosaved_ == version) ||
        now < last_successful_autosave_at_ || now - last_successful_autosave_at_ < interval_) {
        return result;
    }
    if (session_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        result.status = native_project_autosave_status_t::failed;
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
    const auto publication = publish_native_project_recovery_atomic(
        caller_supplied_backup_path, song, std::move(metadata), std::move(options));
    result.diagnostics = publication.diagnostics;
    if (!publication.published || publication.has_errors()) {
        result.status = native_project_autosave_status_t::failed;
        return result;
    }
    last_successful_autosave_at_ = now;
    last_autosaved_ = version;
    ++session_sequence_;
    result.session_sequence = session_sequence_;
    result.status = native_project_autosave_status_t::published;
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

#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project_delta.h>
#include "text_util.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace wormedit {
namespace {
using json = nlohmann::json;
using journal_diagnostic_t = native_project_storage_diagnostic_t;
using journal_code_t = native_project_storage_diagnostic_code_t;
constexpr std::size_t maximum_command_bytes = 1024;
constexpr std::size_t maximum_path_bytes = 4096;

// The dependency parses JSON. The callback bounds the shallow record wrapper,
// including duplicate keys, before its embedded native payload reaches the codec.
auto parse_record_json(std::string_view bytes) -> json {
    std::size_t nodes{};
    std::array<std::set<std::string>, 4> keys;
    return json::parse(bytes, [&](int depth, json::parse_event_t event, json& value) {
        if (depth < 0 || depth >= static_cast<int>(keys.size()) || ++nodes > 64)
            throw std::length_error("journal wrapper exceeds its structural budget");
        auto& seen = keys[static_cast<std::size_t>(depth)];
        if (event == json::parse_event_t::object_start) {
            if (depth + 1 >= static_cast<int>(keys.size())) throw std::length_error("journal nesting budget exceeded");
            keys[static_cast<std::size_t>(depth) + 1].clear();
        }
        if (event == json::parse_event_t::key && !seen.insert(value.get<std::string>()).second)
            throw std::invalid_argument("duplicate journal key");
        return true;
    });
}
auto read_unsigned(const json& value) -> std::uint64_t {
    if (!value.is_number_unsigned()) throw std::invalid_argument("journal integer must be unsigned");
    return value.get<std::uint64_t>();
}
struct journal_replay_t {
    std::optional<native_project_replay_t> tree;
    std::string seed_payload;
    command_journal_read_t seed;
    command_journal_read_t previous;
};
void finish_journal_replay(command_journal_read_t& read, journal_replay_t& replay,
    std::vector<journal_diagnostic_t>& diagnostics, native_project_limits_t limits) {
    if (!replay.tree) return;
    auto parsed = replay.tree->finish();
    if (parsed.value && parsed.ok() && parsed.value->revision) {
        read.snapshot->song = std::move(*parsed.value);
        return;
    }
    diagnostics.push_back({.code = journal_code_t::validation_failure,
        .message = "Reconstructed journal state failed native validation; recovered the latest seed."});
    auto seed = parse_native_project(replay.seed_payload, limits);
    if (seed.value && seed.ok() && seed.value->revision) {
        read = replay.seed;
        read.snapshot->song = std::move(*seed.value);
    } else read = replay.previous;
}
void decode_seed(const json& body, const command_journal_read_t& read,
    native_project_limits_t limits, journal_replay_t& replay) {
    const auto& payload = body.at("payload").get_ref<const std::string&>();
    if (read.format_version == 2 && body.at("payload_checksum") != native_project_content_checksum(payload))
        throw std::invalid_argument("journal seed payload checksum mismatch");
    auto tree = native_project_replay_t::from(payload, limits);
    if (!tree) {
        if (tree.error().code == failure_code_t::capacity_exceeded) throw std::length_error(tree.error().message);
        throw std::invalid_argument(tree.error().message);
    }
    if (read.snapshot && (tree->project() != read.snapshot->song.id ||
        read_unsigned(body.at("sequence")) <= read.last_sequence))
        throw std::invalid_argument("journal sequence or project chain is broken");
    if (read.snapshot && read.format_version == 1 &&
        (read_unsigned(body.at("sequence")) != read.last_sequence + 1 ||
            read_unsigned(body.at("before_revision")) != read.snapshot->song.revision))
        throw std::invalid_argument("journal sequence or revision chain is broken");
    // The caller retains the seed bytes for final-validation fallback.
    replay.tree = std::move(*tree);
}
void decode_delta(const json& body, const command_journal_read_t& read,
    native_project_limits_t limits, journal_replay_t& replay) {
    const auto& payload = body.at("payload").get_ref<const std::string&>();
    if (body.at("kind") != "delta" || !read.snapshot || !replay.tree || read.deltas_since_seed >= 128 ||
        read_unsigned(body.at("base_sequence")) != read.last_sequence || body.at("base_checksum") != read.last_checksum ||
        read_unsigned(body.at("sequence")) != read.last_sequence + 1 ||
        read_unsigned(body.at("before_revision")) != read.snapshot->song.revision)
        throw std::invalid_argument("journal delta has no matching base");
    const auto delta = native_project_delta_t::parse(payload, limits);
    if (!delta) {
        if (delta.error().code == failure_code_t::capacity_exceeded) throw std::length_error(delta.error().message);
        throw std::invalid_argument(delta.error().message);
    }
    const auto applied = replay.tree->apply(*delta);
    if (!applied) {
        if (applied.error().code == failure_code_t::capacity_exceeded) throw std::length_error(applied.error().message);
        throw std::invalid_argument(applied.error().message);
    }
}
auto decode_post_state(const json& body, const command_journal_read_t& read,
    native_project_limits_t limits, journal_replay_t& replay) -> song_document_t {
    if (read.format_version == 1 || body.at("kind") == "seed") decode_seed(body, read, limits, replay);
    else decode_delta(body, read, limits, replay);
    return {.id = replay.tree->project(), .revision = replay.tree->revision()};
}

auto decode_entry(const json& root, const command_journal_read_t& read, native_project_limits_t codec_limits, journal_replay_t& replay) -> command_journal_entry_t {
    if (!root.is_object() || root.size() != 2) throw std::invalid_argument("invalid journal record shape");
    const auto& body = root.at("record");
    const auto expected = read.format_version == 1 ? 9U : body.at("kind") == "seed" ? 11U : 12U;
    if (!body.is_object() || body.size() != expected) throw std::invalid_argument("invalid journal body shape");
    command_journal_entry_t entry{.sequence = read_unsigned(body.at("sequence")),
        .before_revision = read_unsigned(body.at("before_revision")), .command = body.at("command").get<std::string>()};
    if (!entry.sequence || !entry.before_revision || !text_util::is_valid_bounded_text(entry.command, maximum_command_bytes))
        throw std::invalid_argument("invalid journal identity or command");
    const auto& saved = body.at("saved_revision");
    if (!saved.is_null()) entry.snapshot.metadata.last_explicit_save_revision = read_unsigned(saved);
    const auto& source = body.at("source_path").get_ref<const std::string&>();
    const auto& intended = body.at("intended_path").get_ref<const std::string&>();
    if (!text_util::is_valid_bounded_text(source, maximum_path_bytes) || !text_util::is_valid_bounded_text(intended, maximum_path_bytes))
        throw std::invalid_argument("journal source paths exceed their budget");
    entry.snapshot.metadata.original_source_path = text_util::path_from_utf8(source);
    entry.snapshot.metadata.intended_project_path = text_util::path_from_utf8(intended);
    const auto timestamp = read_unsigned(body.at("utc_milliseconds"));
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::duration::max());
    if (timestamp > static_cast<std::uint64_t>(duration.count())) throw std::invalid_argument("invalid journal timestamp");
    entry.snapshot.metadata.utc_write_time = std::chrono::system_clock::time_point{std::chrono::milliseconds{timestamp}};
    entry.snapshot.metadata.session_sequence = entry.sequence;
    const auto& journal_path = body.at("journal_path");
    if (!journal_path.is_null()) {
        const auto path = journal_path.get<std::string>();
        if (path.empty() || !text_util::is_valid_bounded_text(path, maximum_path_bytes)) throw std::invalid_argument("invalid journal path");
        entry.snapshot.metadata.journal_position = native_project_journal_position_t{
            .path = text_util::path_from_utf8(path), .sequence = entry.sequence};
    }
    entry.snapshot.song = decode_post_state(body, read, codec_limits, replay);
    return entry;
}
struct encoded_entry_t {
    std::string bytes;
    std::string checksum;
    bool is_seed{true};
    std::size_t minimum_seed_bytes{};
};
auto make_record_body(const command_journal_entry_t& entry) -> result_t<json, error_t<failure_code_t>> {
    if (!entry.sequence || !entry.before_revision || !entry.snapshot.song.revision ||
        !text_util::is_valid_bounded_text(entry.command, maximum_command_bytes))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, "Invalid journal identity or command."});
    const auto& metadata = entry.snapshot.metadata;
    const auto source = text_util::path_utf8(metadata.original_source_path);
    const auto intended = text_util::path_utf8(metadata.intended_project_path);
    const auto journal = metadata.journal_position ? text_util::path_utf8(metadata.journal_position->path) : std::string{};
    if (!text_util::is_valid_bounded_text(source, maximum_path_bytes) || !text_util::is_valid_bounded_text(intended, maximum_path_bytes) ||
        (metadata.journal_position && (journal.empty() || !text_util::is_valid_bounded_text(journal, maximum_path_bytes))))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, "Invalid journal source paths."});
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(metadata.utc_write_time.time_since_epoch()).count();
    if (timestamp < 0) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data,
        "Journal timestamp predates its supported epoch."});
    return json{{"sequence", entry.sequence}, {"before_revision", entry.before_revision}, {"command", entry.command},
        {"payload", ""}, {"saved_revision", metadata.last_explicit_save_revision ? json(*metadata.last_explicit_save_revision) : json(nullptr)},
        {"source_path", source}, {"intended_path", intended}, {"utc_milliseconds", static_cast<std::uint64_t>(timestamp)},
        {"journal_path", metadata.journal_position ? json(journal) : json(nullptr)}};
}
auto encode_seed_payload(json& body, const command_journal_entry_t& entry, std::size_t maximum_bytes)
    -> result_t<std::size_t, error_t<failure_code_t>> {
    body["kind"] = "seed";
    body["payload_checksum"] = std::string(64, '0');
    const auto overhead = json{{"record", body}, {"sha256", std::string(64, '0')}}.dump().size() + 1;
    const auto payload_budget = maximum_bytes > overhead ? (maximum_bytes - overhead) / 2 : 0;
    const auto payload = write_native_project(entry.snapshot.song, {.maximum_source_bytes = payload_budget});
    if (!payload.value || !payload.ok()) {
        const auto& diagnostic = payload.diagnostics.front();
        return std::unexpected(error_t<failure_code_t>{
            diagnostic.code == native_project_diagnostic_code_t::resource_limit
                ? failure_code_t::capacity_exceeded : failure_code_t::invalid_data,
            "Journal post-state serialization failed: " + diagnostic.message});
    }
    body["payload"] = *payload.value;
    body["payload_checksum"] = native_project_content_checksum(*payload.value);
    return payload.value->size();
}
auto can_encode_delta(const command_journal_read_t& read, const command_journal_entry_t& entry, bool force_seed) -> bool {
    return !force_seed && read.snapshot && read.format_version == 2 && read.deltas_since_seed < 128 &&
        read.last_sequence != std::numeric_limits<std::uint64_t>::max() && entry.sequence == read.last_sequence + 1 &&
        entry.before_revision == read.snapshot->song.revision && entry.snapshot.song.id == read.snapshot->song.id;
}
auto encode_entry(const command_journal_entry_t& entry, const command_journal_read_t& read,
    std::size_t maximum_bytes, bool force_seed) -> result_t<encoded_entry_t, error_t<failure_code_t>> {
    auto body = make_record_body(entry);
    if (!body) return std::unexpected(body.error());
    encoded_entry_t result;
    if (can_encode_delta(read, entry, force_seed)) {
        const auto delta = native_project_delta_t::from(read.snapshot->song, entry.snapshot.song);
        if (delta) {
            auto candidate = *body;
            candidate["kind"] = "delta";
            candidate["payload"] = delta->bytes();
            candidate["base_sequence"] = read.last_sequence;
            candidate["base_checksum"] = read.last_checksum;
            const auto size = json{{"record", candidate}, {"sha256", std::string(64, '0')}}.dump().size() + 1;
            if (size <= delta->minimum_result_bytes(read.minimum_seed_bytes) / 2) {
                *body = std::move(candidate);
                result.is_seed = false;
                result.minimum_seed_bytes = delta->minimum_result_bytes(read.minimum_seed_bytes);
            }
        }
    }
    if (result.is_seed) {
        const auto size = encode_seed_payload(*body, entry, maximum_bytes);
        if (!size) return std::unexpected(size.error());
        result.minimum_seed_bytes = *size;
    }
    result.checksum = native_project_content_checksum(body->dump());
    result.bytes = json{{"record", *body}, {"sha256", result.checksum}}.dump() + '\n';
    return result;
}
auto validate_next_entry(const command_journal_read_t& read, const command_journal_entry_t& entry,
    bool is_seed) -> void {
    if (!read.snapshot) {
        if (!is_seed) throw std::invalid_argument("journal segment must start with a seed");
        return;
    }
    if (entry.snapshot.song.id != read.snapshot->song.id || entry.sequence <= read.last_sequence)
        throw std::invalid_argument("journal sequence or project chain is broken");
    if ((!is_seed || read.format_version == 1) &&
        (read.last_sequence == std::numeric_limits<std::uint64_t>::max() || entry.sequence != read.last_sequence + 1 ||
            entry.before_revision != read.snapshot->song.revision))
        throw std::invalid_argument("journal sequence or revision chain is broken");
}
void accept_journal_entry(command_journal_read_t& read, command_journal_entry_t entry, std::size_t end,
    bool is_seed, std::size_t minimum_seed_bytes) {
    read.revisions.push_back(entry.before_revision);
    read.last_sequence = entry.sequence;
    read.last_command = entry.command;
    read.last_before_revision = entry.before_revision;
    read.deltas_since_seed = is_seed ? 0 : read.deltas_since_seed + 1;
    read.minimum_seed_bytes = minimum_seed_bytes;
    read.snapshot = std::move(entry.snapshot);
    // Keep the accepted source schema. A seed may migrate on disk; its original
    // in-memory values must not become a delta base merely by relabeling them.
    auto& metadata = read.snapshot->metadata;
    metadata.utc_write_time = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::milliseconds>(metadata.utc_write_time.time_since_epoch())};
    metadata.session_sequence = entry.sequence;
    if (metadata.journal_position) metadata.journal_position->sequence = entry.sequence;
    read.revisions.push_back(read.snapshot->song.revision);
    ++read.accepted_records;
    read.positions.push_back({.sequence = entry.sequence, .end_byte = end, .is_seed = is_seed});
}
auto read_journal_line(command_journal_read_t& read, std::string_view bytes, std::size_t end,
    std::vector<journal_diagnostic_t>& diagnostics, native_project_limits_t codec_limits, journal_replay_t& replay) -> bool {
    try {
        const auto root = parse_record_json(bytes.substr(read.valid_bytes, end - read.valid_bytes));
        if (!root.is_object() || root.size() != 2) throw std::invalid_argument("invalid journal record shape");
        const auto checksum = root.at("sha256").get<std::string>();
        if (native_project_content_checksum(root.at("record").dump()) != checksum) {
            diagnostics.push_back({.code = journal_code_t::checksum_mismatch, .message = "Journal record checksum mismatch."});
            return false;
        }
        const auto& body = root.at("record");
        const bool is_seed = read.format_version == 1 || body.at("kind") == "seed";
        const auto sequence = read_unsigned(body.at("sequence"));
        if (read.snapshot && sequence == read.last_sequence && checksum == read.last_checksum) {
            read.positions.push_back({.sequence = sequence, .end_byte = end + 1, .is_seed = is_seed});
        } else {
            if (is_seed && replay.tree) {
                const auto prior_diagnostics = diagnostics.size();
                finish_journal_replay(read, replay, diagnostics, codec_limits);
                if (diagnostics.size() != prior_diagnostics) { replay.tree.reset(); return false; }
                replay.previous = read;
            }
            auto entry = decode_entry(root, read, codec_limits, replay);
            validate_next_entry(read, entry, is_seed);
            // Migration validates the snapshot, but cannot change its encoded delta base.
            accept_journal_entry(read, std::move(entry), end + 1, is_seed,
                is_seed && replay.tree->can_apply_delta() ? body.at("payload").get_ref<const std::string&>().size() : 0);
        }
        read.last_checksum = checksum;
        read.valid_bytes = end + 1;
        if (is_seed) {
            replay.seed = read;
            // Preserve bytes independently of the parsed wrapper's lifetime.
            replay.seed_payload = body.at("payload").get<std::string>();
        }
        return true;
    } catch (const std::length_error& exception) {
        read.stopped_by_limit = true;
        diagnostics.push_back({.code = journal_code_t::resource_limit, .message = exception.what()});
    } catch (const std::exception& exception) {
        diagnostics.push_back({.code = journal_code_t::validation_failure, .message = exception.what()});
    }
    return false;
}
}

auto command_journal_read_t::has_header(std::string_view bytes) -> bool {
    return bytes.starts_with(command_journal_header) || bytes.starts_with(command_journal_v1_header);
}

auto command_journal_read_t::from(std::string_view bytes, command_journal_limits_t limits, native_project_limits_t codec_limits)
    -> diagnosed_t<command_journal_read_t, std::vector<journal_diagnostic_t>> {
    diagnosed_t<command_journal_read_t, std::vector<journal_diagnostic_t>> result;
    auto& read = result.value;
    if (bytes.size() > limits.maximum_bytes || limits.maximum_bytes > command_journal_limits_t::maximum_supported_bytes ||
        limits.maximum_records > command_journal_limits_t::maximum_supported_records ||
        limits.maximum_record_bytes > command_journal_limits_t::maximum_supported_record_bytes) {
        read.stopped_by_limit = true;
        result.diagnostics.push_back({.code = journal_code_t::resource_limit, .message = "Journal exceeds its configured limits."});
        return result;
    }
    if (!has_header(bytes)) {
        result.diagnostics.push_back({.code = journal_code_t::validation_failure, .message = "Unsupported command journal header."});
        return result;
    }
    read.format_version = bytes.starts_with(command_journal_v1_header) ? 1 : 2;
    read.valid_bytes = command_journal_header.size();
    journal_replay_t replay;
    replay.previous = read;
    while (read.valid_bytes < bytes.size()) {
        const auto end = bytes.find('\n', read.valid_bytes);
        if (read.positions.size() >= limits.maximum_records || (end != std::string_view::npos && end - read.valid_bytes > limits.maximum_record_bytes)) {
            read.stopped_by_limit = true;
            result.diagnostics.push_back({.code = journal_code_t::resource_limit, .message = "Journal record budget exceeded."});
            break;
        }
        if (end == std::string_view::npos) {
            result.diagnostics.push_back({.code = journal_code_t::validation_failure, .message = "Journal has a torn final record."});
            break;
        }
        if (!read_journal_line(read, bytes, end, result.diagnostics, codec_limits, replay)) break;
    }
    finish_journal_replay(read, replay, result.diagnostics, codec_limits);
    return result;
}

auto command_journal_read_t::replay_on(const song_document_t& baseline) const -> diagnosed_t<song_document_t> {
    if (!snapshot) return {.value = baseline};
    if (snapshot->song.id != baseline.id || std::ranges::find(revisions, baseline.revision) == revisions.end())
        return {.value = baseline, .diagnostics = {error(diagnostic_code_t::invalid_binding, "Journal does not continue this recovery baseline.")}};
    return {.value = baseline.revision == snapshot->song.revision ? baseline : snapshot->song};
}

command_journal_t::command_journal_t(std::filesystem::path path, command_journal_limits_t limits)
    : path_(std::move(path)), limits_(limits) {}

auto command_journal_t::read_bytes() const -> result_t<file_contents_t, std::string> {
    if (limits_.maximum_bytes > command_journal_limits_t::maximum_supported_bytes)
        return std::unexpected("Journal byte budget exceeds its hard limit.");
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error == std::errc::no_such_file_or_directory) return file_contents_t{.bytes = std::string{command_journal_header}};
    if (error) return std::unexpected(error.message());
    if (size > limits_.maximum_bytes) return std::unexpected("Journal file exceeds its byte budget.");
    std::ifstream input(path_, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || input.peek() != std::char_traits<char>::eof())
        return std::unexpected("Journal file could not be read consistently.");
    return file_contents_t{.bytes = std::move(bytes), .source_size = size};
}
auto command_journal_t::read() const -> diagnosed_t<command_journal_read_t, std::vector<journal_diagnostic_t>> {
    validated_.reset();
    const auto bytes = read_bytes();
    if (!bytes) return {.diagnostics = {{.code = journal_code_t::read_failure, .message = bytes.error(), .path = path_}}};
    auto read = command_journal_read_t::from(bytes->bytes, limits_);
    if (read.ok()) validated_ = validated_file_t{.read = read.value, .source_size = bytes->source_size};
    return read;
}
namespace {
auto is_identical_retry(const command_journal_read_t& read, const command_journal_entry_t& entry) -> bool {
    if (!read.snapshot || entry.sequence != read.last_sequence) return false;
    const command_journal_entry_t accepted{.sequence = read.last_sequence, .before_revision = read.last_before_revision,
        .command = read.last_command, .snapshot = *read.snapshot};
    const auto expected = make_record_body(accepted), actual = make_record_body(entry);
    if (!actual || !expected || *actual != *expected) return false;
    const auto delta = native_project_delta_t::from(read.snapshot->song, entry.snapshot.song);
    if (delta) return delta->is_empty();
    const auto before = write_native_project(read.snapshot->song);
    const auto after = write_native_project(entry.snapshot.song);
    return before.value && after.value && *before.value == *after.value;
}
}
auto command_journal_t::append(const command_journal_entry_t& entry) const -> result_t<void, error_t<failure_code_t>> {
    const bool force_seed = requires_seed_;
    requires_seed_ = true;
    if (!validated_) {
        const auto loaded = read();
        if (!loaded.ok()) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data,
            "Journal prefix does not validate; checkpoint required."});
    }
    auto& cached = *validated_;
    if (is_identical_retry(cached.read, entry)) { requires_seed_ = force_seed; return {}; }
    if (cached.read.format_version != 2) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data,
        "Legacy journal requires a checkpoint before appending."});
    const auto encoded = encode_entry(entry, cached.read, limits_.maximum_record_bytes, force_seed);
    if (!encoded) return std::unexpected(encoded.error());
    try { validate_next_entry(cached.read, entry, encoded->is_seed); }
    catch (const std::exception& exception) { return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data, exception.what()}); }
    const auto output = cached.source_size ? encoded->bytes : std::string{command_journal_header} + encoded->bytes;
    if (cached.read.positions.size() >= limits_.maximum_records || encoded->bytes.size() > limits_.maximum_record_bytes ||
        output.size() > limits_.maximum_bytes || cached.source_size > limits_.maximum_bytes - output.size())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
            "Journal capacity exceeded; checkpoint required."});
    const auto appended = append_native_bytes_file(path_, output, cached.source_size, limits_.maximum_bytes);
    if (!appended) { validated_.reset(); return std::unexpected(error_t<failure_code_t>{failure_code_t::publication_failed, appended.error()}); }
    cached.source_size += output.size();
    accept_journal_entry(cached.read, entry, static_cast<std::size_t>(cached.source_size), encoded->is_seed, encoded->minimum_seed_bytes);
    cached.read.last_checksum = encoded->checksum;
    cached.read.valid_bytes = static_cast<std::size_t>(cached.source_size);
    requires_seed_ = false;
    return {};
}
auto command_journal_t::accepted_state() const -> const command_journal_read_t* {
    return validated_ ? &validated_->read : nullptr;
}
auto command_journal_t::checkpoint(std::uint64_t through_sequence) const -> result_t<void, std::string> {
    validated_.reset();
    requires_seed_ = true;
    const auto bytes = read_bytes();
    if (!bytes) return std::unexpected(bytes.error());
    const auto read = command_journal_read_t::from(bytes->bytes, limits_);
    if (read.value.stopped_by_limit) return std::unexpected("Journal cannot be compacted through an unread limit.");
    if (!read.value.snapshot && !read.ok() && !through_sequence)
        return std::unexpected("Journal has no validated prefix to checkpoint.");
    std::size_t end = command_journal_header.size();
    std::size_t record_start = end, seed_start = end;
    for (const auto& position : read.value.positions) {
        if (position.is_seed) seed_start = record_start;
        if (position.sequence > through_sequence) {
            end = position.is_seed ? record_start : seed_start;
            break;
        }
        record_start = position.end_byte;
    }
    if (!read.value.snapshot || read.value.last_sequence <= through_sequence) {
        std::error_code error;
        std::filesystem::remove(path_, error);
        if (error) return std::unexpected(error.message());
        return {};
    }
    if (end == command_journal_header.size()) return {};
    const auto compacted = create_native_bytes_file(path_, bytes->bytes.substr(0, command_journal_header.size()) + bytes->bytes.substr(end),
        {.codec_limits = {.maximum_source_bytes = limits_.maximum_bytes}});
    if (!compacted.value || !compacted.ok()) return std::unexpected("Journal compaction failed; the previous prefix was retained.");
    return {};
}
} // namespace wormedit

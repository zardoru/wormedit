#include <wormedit/core/command_journal.h>
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
auto decode_entry(const json& root, native_project_limits_t codec_limits) -> command_journal_entry_t {
    if (!root.is_object() || root.size() != 2) throw std::invalid_argument("invalid journal record shape");
    const auto& body = root.at("record");
    if (!body.is_object() || body.size() != 9) throw std::invalid_argument("invalid journal body shape");
    command_journal_entry_t entry{.sequence = read_unsigned(body.at("sequence")),
        .before_revision = read_unsigned(body.at("before_revision")), .command = body.at("command").get<std::string>()};
    if (!entry.sequence || !entry.before_revision || !text_util::is_valid_bounded_text(entry.command, 1024))
        throw std::invalid_argument("invalid journal identity or command");
    const auto parsed = parse_native_project(body.at("payload").get_ref<const std::string&>(), codec_limits);
    if (!parsed.value || !parsed.ok() || !parsed.value->revision) {
        if (std::ranges::any_of(parsed.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == native_project_diagnostic_code_t::resource_limit;
        })) throw std::length_error("journal post-state exceeds codec limits");
        throw std::invalid_argument("journal post-state does not validate");
    }
    entry.snapshot.song = *parsed.value;
    const auto& saved = body.at("saved_revision");
    if (!saved.is_null()) entry.snapshot.metadata.last_explicit_save_revision = read_unsigned(saved);
    const auto& source = body.at("source_path").get_ref<const std::string&>();
    const auto& intended = body.at("intended_path").get_ref<const std::string&>();
    if (!text_util::is_valid_bounded_text(source, 4096) || !text_util::is_valid_bounded_text(intended, 4096))
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
        if (path.empty() || !text_util::is_valid_bounded_text(path, 4096)) throw std::invalid_argument("invalid journal path");
        entry.snapshot.metadata.journal_position = native_project_journal_position_t{
            .path = text_util::path_from_utf8(path), .sequence = entry.sequence};
    }
    return entry;
}
auto encode_entry(const command_journal_entry_t& entry, std::size_t maximum_bytes) -> result_t<std::string, std::string> {
    if (!entry.sequence || !entry.before_revision || !text_util::is_valid_bounded_text(entry.command, 1024))
        return std::unexpected("Invalid journal identity or command.");
    const auto payload = write_native_project(entry.snapshot.song, {.maximum_source_bytes = maximum_bytes});
    if (!payload.value || !payload.ok()) return std::unexpected("Journal post-state serialization failed.");
    const auto& metadata = entry.snapshot.metadata;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(metadata.utc_write_time.time_since_epoch()).count();
    if (timestamp < 0) return std::unexpected("Journal timestamp predates its supported epoch.");
    json body{{"sequence", entry.sequence}, {"before_revision", entry.before_revision}, {"command", entry.command},
        {"payload", *payload.value}, {"saved_revision", metadata.last_explicit_save_revision ? json(*metadata.last_explicit_save_revision) : json(nullptr)},
        {"source_path", text_util::path_utf8(metadata.original_source_path)},
        {"intended_path", text_util::path_utf8(metadata.intended_project_path)},
        {"utc_milliseconds", static_cast<std::uint64_t>(timestamp)},
        {"journal_path", metadata.journal_position ? json(text_util::path_utf8(metadata.journal_position->path)) : json(nullptr)}};
    return json{{"record", body}, {"sha256", native_project_content_checksum(body.dump())}}.dump() + '\n';
}
auto validate_next_entry(const command_journal_read_t& read, const command_journal_entry_t& entry,
    std::string_view checksum, std::string_view previous_checksum) -> bool {
    if (!read.snapshot) return false;
    if (entry.sequence == read.last_sequence && checksum == previous_checksum) return true;
    if (read.last_sequence == std::numeric_limits<std::uint64_t>::max() || entry.sequence != read.last_sequence + 1 ||
        entry.before_revision != read.snapshot->song.revision || entry.snapshot.song.id != read.snapshot->song.id)
        throw std::invalid_argument("journal sequence, project, or revision chain is broken");
    return false;
}
void accept_journal_entry(command_journal_read_t& read, command_journal_entry_t entry, std::size_t end) {
    read.revisions.push_back(entry.before_revision);
    read.last_sequence = entry.sequence;
    read.snapshot = std::move(entry.snapshot);
    read.revisions.push_back(read.snapshot->song.revision);
    ++read.accepted_records;
    read.positions.push_back({.sequence = entry.sequence, .end_byte = end});
}
auto read_journal_line(command_journal_read_t& read, std::string_view bytes, std::size_t end,
    std::vector<journal_diagnostic_t>& diagnostics, native_project_limits_t codec_limits) -> bool {
    try {
        const auto root = parse_record_json(bytes.substr(read.valid_bytes, end - read.valid_bytes));
        const auto checksum = root.at("sha256").get<std::string>();
        if (native_project_content_checksum(root.at("record").dump()) != checksum) {
            diagnostics.push_back({.code = journal_code_t::checksum_mismatch, .message = "Journal record checksum mismatch."});
            return false;
        }
        auto entry = decode_entry(root, codec_limits);
        if (!validate_next_entry(read, entry, checksum, read.last_checksum))
            accept_journal_entry(read, std::move(entry), end + 1);
        else read.positions.push_back({.sequence = entry.sequence, .end_byte = end + 1});
        read.last_checksum = checksum;
        read.valid_bytes = end + 1;
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

auto command_journal_read_t::from(std::string_view bytes, command_journal_limits_t limits, native_project_limits_t codec_limits)
    -> diagnosed_t<command_journal_read_t, std::vector<journal_diagnostic_t>> {
    diagnosed_t<command_journal_read_t, std::vector<journal_diagnostic_t>> result;
    auto& read = result.value;
    if (bytes.size() > limits.maximum_bytes || limits.maximum_bytes > 64U * 1024U * 1024U ||
        limits.maximum_records > 1024 || limits.maximum_record_bytes > 64U * 1024U * 1024U) {
        read.stopped_by_limit = true;
        result.diagnostics.push_back({.code = journal_code_t::resource_limit, .message = "Journal exceeds its configured limits."});
        return result;
    }
    if (!bytes.starts_with(command_journal_header)) {
        result.diagnostics.push_back({.code = journal_code_t::validation_failure, .message = "Unsupported command journal header."});
        return result;
    }
    read.valid_bytes = command_journal_header.size();
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
        if (!read_journal_line(read, bytes, end, result.diagnostics, codec_limits)) break;
    }
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
    if (limits_.maximum_bytes > 64U * 1024U * 1024U) return std::unexpected("Journal byte budget exceeds its hard limit.");
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
auto command_journal_t::append(const command_journal_entry_t& entry) const -> result_t<void, std::string> {
    if (!validated_) {
        const auto loaded = read();
        if (!loaded.ok()) return std::unexpected("Journal prefix does not validate; checkpoint required.");
    }
    const auto encoded = encode_entry(entry, limits_.maximum_record_bytes);
    if (!encoded) return std::unexpected(encoded.error());
    auto record = command_journal_read_t::from(std::string{command_journal_header} + *encoded, limits_);
    if (!record.ok() || !record.value.snapshot) return std::unexpected("Journal entry does not validate.");
    auto& cached = *validated_;
    try { if (validate_next_entry(cached.read, entry, record.value.last_checksum, cached.read.last_checksum)) return {}; }
    catch (const std::exception& exception) { return std::unexpected(exception.what()); }
    const auto output = cached.source_size ? *encoded : std::string{command_journal_header} + *encoded;
    if (cached.read.positions.size() >= limits_.maximum_records || encoded->size() > limits_.maximum_record_bytes ||
        output.size() > limits_.maximum_bytes || cached.source_size > limits_.maximum_bytes - output.size())
        return std::unexpected("Journal capacity exceeded; checkpoint required.");
    const auto appended = append_native_bytes_file(path_, output, cached.source_size, limits_.maximum_bytes);
    if (!appended) { validated_.reset(); return std::unexpected(appended.error()); }
    cached.source_size += output.size();
    accept_journal_entry(cached.read, {.sequence = entry.sequence, .before_revision = entry.before_revision,
        .snapshot = std::move(*record.value.snapshot)}, static_cast<std::size_t>(cached.source_size));
    cached.read.last_checksum = std::move(record.value.last_checksum);
    cached.read.valid_bytes = static_cast<std::size_t>(cached.source_size);
    return {};
}
auto command_journal_t::checkpoint(std::uint64_t through_sequence) const -> result_t<void, std::string> {
    validated_.reset();
    const auto bytes = read_bytes();
    if (!bytes) return std::unexpected(bytes.error());
    const auto read = command_journal_read_t::from(bytes->bytes, limits_);
    if (read.value.stopped_by_limit) return std::unexpected("Journal cannot be compacted through an unread limit.");
    if (!read.value.snapshot && !read.ok() && !through_sequence)
        return std::unexpected("Journal has no validated prefix to checkpoint.");
    std::size_t end = command_journal_header.size();
    for (const auto& position : read.value.positions)
        if (position.sequence <= through_sequence) end = position.end_byte;
    if (!read.value.snapshot || read.value.last_sequence <= through_sequence) {
        std::error_code error;
        std::filesystem::remove(path_, error);
        if (error) return std::unexpected(error.message());
        return {};
    }
    if (end == command_journal_header.size()) return {};
    const auto compacted = create_native_bytes_file(path_, std::string{command_journal_header} + bytes->bytes.substr(end),
        {.codec_limits = {.maximum_source_bytes = limits_.maximum_bytes}});
    if (!compacted.value || !compacted.ok()) return std::unexpected("Journal compaction failed; the previous prefix was retained.");
    return {};
}
} // namespace wormedit

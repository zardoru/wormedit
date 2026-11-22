#include <wormedit/core/update_check.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>

namespace wormedit {
namespace {

using json_t = nlohmann::json;

auto read_text(const json_t& object, const char* key, std::string& out, bool required) -> std::string {
    const auto found = object.find(key);
    if (found == object.end()) return required ? std::string{"Missing \""} + key + "\"." : std::string{};
    if (!found->is_string()) return std::string{"\""} + key + "\" must be text.";
    out = found->get<std::string>();
    if (out.size() > maximum_update_field_bytes) return std::string{"\""} + key + "\" is too long.";
    return {};
}

auto is_https_url(std::string_view url) -> bool {
    return url.starts_with("https://") && url.size() > 8 &&
           std::ranges::none_of(url, [](unsigned char c) { return c <= ' ' || c == 0x7f; });
}

} // namespace

auto update_channel_key(update_channel_t channel) -> std::string_view {
    switch (channel) {
    case update_channel_t::stable: return "stable";
    }
    return "stable";
}

auto parse_update_manifest(std::string_view bytes, update_channel_t channel)
    -> result_t<update_manifest_entry_t, std::string> {
    if (bytes.size() > maximum_update_manifest_bytes) return std::unexpected{"The manifest is too large."};
    const auto root = json_t::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (!root.is_object()) return std::unexpected{"The manifest is not a JSON object."};
    const auto schema = root.find("schema");
    if (schema == root.end() || !schema->is_number_integer() || *schema != 1)
        return std::unexpected{"Unsupported manifest schema."};
    const auto channels = root.find("channels");
    if (channels == root.end() || !channels->is_object()) return std::unexpected{"The manifest has no channels."};
    const auto found = channels->find(std::string{update_channel_key(channel)});
    if (found == channels->end() || !found->is_object()) return std::unexpected{"The channel is not published."};

    update_manifest_entry_t entry;
    for (const auto& error : {read_text(*found, "version", entry.version, true),
                              read_text(*found, "url", entry.url, true),
                              read_text(*found, "notes_url", entry.notes_url, false),
                              read_text(*found, "published", entry.published, false)})
        if (!error.empty()) return std::unexpected{error};
    if (!release_version_t::from(entry.version)) return std::unexpected{"The published version is not valid."};
    if (!is_https_url(entry.url)) return std::unexpected{"The download address must be https."};
    if (!entry.notes_url.empty() && !is_https_url(entry.notes_url))
        return std::unexpected{"The release notes address must be https."};
    return entry;
}

auto evaluate_update(std::string_view current_version, const update_manifest_entry_t& latest)
    -> update_decision_t {
    const auto current = release_version_t::from(current_version);
    const auto published = release_version_t::from(latest.version);
    if (!current || !published || current->is_nightly()) return update_decision_t::unsupported_build;
    return published->compare(*current) > 0 ? update_decision_t::available : update_decision_t::up_to_date;
}

auto is_update_check_due(std::int64_t last_check_unix_seconds, std::int64_t now_unix_seconds) -> bool {
    if (last_check_unix_seconds <= 0 || now_unix_seconds < last_check_unix_seconds) return true;
    const auto interval = std::chrono::duration_cast<std::chrono::seconds>(update_check_interval).count();
    return now_unix_seconds - last_check_unix_seconds >= interval;
}

auto parse_update_check_state(std::string_view text) -> std::int64_t {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
    std::int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value > 0 ? value : 0;
}

auto serialize_update_check_state(std::int64_t last_check_unix_seconds) -> std::string {
    return std::to_string(last_check_unix_seconds) + "\n";
}

} // namespace wormedit

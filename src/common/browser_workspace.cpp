#include "common/browser_workspace.h"

#include "core/resource_asset_relinks.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <atomic>
#include <charconv>
#include <emscripten.h>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <string>
#include <utility>

namespace {

using namespace wormedit;
using nlohmann::json;

struct asset_snapshot_key_t {
    document_stamp_t document;
    std::filesystem::path base;
    std::uint64_t session;
    bool loaded;
    bool busy;
    auto operator==(const asset_snapshot_key_t&) const -> bool = default;
};

struct asset_snapshot_t {
    std::optional<asset_snapshot_key_t> key;
    std::map<asset_id_t, std::string> missing;
    unsigned generation{};
};

std::atomic_bool watching{false};
std::mutex request_mutex;
std::optional<std::string> request;
asset_snapshot_t snapshot;

auto audio_file_exists(const std::filesystem::path& path) -> bool {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

auto display_path(const std::filesystem::path& source, const std::filesystem::path& base)
    -> std::filesystem::path {
    if (!source.is_absolute()) return source;
    const auto relative = source.lexically_relative(base);
    if (relative.empty() || *relative.begin() == "..") return source.filename();
    return relative;
}

void publish_assets(asset_snapshot_key_t key, const song_document_t& song) {
    std::map<asset_id_t, std::string> missing;
    if (key.loaded) {
        for (const auto& asset : song.audio_assets) {
            if (asset.source_path.empty() || audio_file_exists(key.base / asset.source_path)) continue;
            missing.emplace(asset.id, display_path(asset.source_path, key.base).generic_string());
        }
    }
    // Notes, BMS branch selection and job progress need not invalidate a file choice.
    if (!snapshot.key || snapshot.key->session != key.session || snapshot.key->base != key.base ||
        snapshot.missing != missing)
        ++snapshot.generation;
    snapshot.key = std::move(key);
    snapshot.missing = std::move(missing);
    auto sounds = json::array();
    for (const auto& [asset, path] : snapshot.missing)
        sounds.push_back({{"id", std::to_string(asset.value)}, {"path", path}});
    const auto message = json{{"generation", snapshot.generation}, {"loaded", snapshot.key->loaded},
                              {"busy", snapshot.key->busy}, {"session", std::to_string(snapshot.key->session)},
                              {"sounds", std::move(sounds)}}.dump(-1, ' ', false, json::error_handler_t::replace);
    MAIN_THREAD_EM_ASM({ Module.browserAssets.update(UTF8ToString($0)); }, message.c_str());
}

auto replacement_path(const json& item) -> std::filesystem::path {
    const auto path = std::filesystem::path(item.at("path").get<std::string>()).lexically_normal();
    const auto text = path.generic_string();
    if (!text.starts_with("/imports/selection-") || !audio_file_exists(path))
        throw std::runtime_error("A selected sound is no longer available.");
    return path;
}

auto apply_repair(const std::string& payload, document_history_t& history,
                  const asset_snapshot_key_t& key) -> std::string {
    const auto input = json::parse(payload);
    if (!key.loaded || key.busy) return "Wait for the current project operation to finish.";
    if (input.at("generation").get<unsigned>() != snapshot.generation)
        return "The project changed. Review the matches and try again.";
    const auto& matches = input.at("matches");
    if (!matches.is_array() || matches.empty() || matches.size() > 4096)
        return "Select up to 4096 sounds to relink.";
    std::set<asset_id_t> selected;
    std::vector<relink_resource_asset_action_t> actions;
    for (const auto& match : matches) {
        const auto& text = match.at("id").get_ref<const std::string&>();
        asset_id_t asset;
        const auto end = text.data() + text.size();
        const auto [position, error] = std::from_chars(text.data(), end, asset.value);
        if (error != std::errc{} || position != end || !snapshot.missing.contains(asset) ||
            !selected.insert(asset).second)
            return "The selected asset list was invalid.";
        actions.push_back({.asset = asset, .source_path = replacement_path(match)});
    }
    const auto result = apply_resource_asset_relinks(history, actions);
    if (!result.applied)
        return result.diagnostics.empty() ? "No sounds changed." : result.diagnostics.front().message;
    return "Relinked " + std::to_string(actions.size()) + (actions.size() == 1 ? " sound." : " sounds.");
}

void process_repair(const std::string& payload, document_history_t& history,
                    const asset_snapshot_key_t& key) {
    std::string message;
    try {
        message = apply_repair(payload, history, key);
    } catch (const std::exception& error) {
        message = error.what();
    }
    MAIN_THREAD_EM_ASM({ Module.browserAssets.complete(UTF8ToString($0)); }, message.c_str());
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void wormedit_browser_reset_keyboard() {
    SDL_RunOnMainThread([](void*) { SDL_ResetKeyboard(); }, nullptr, false);
}

extern "C" EMSCRIPTEN_KEEPALIVE void wormedit_browser_watch_assets(int enabled) {
    watching.store(enabled != 0);
}

extern "C" EMSCRIPTEN_KEEPALIVE int wormedit_browser_repair_assets(const char* payload) {
    std::lock_guard lock(request_mutex);
    if (request) return 0;
    request = payload;
    return 1;
}

namespace wormedit::platform {
void show_browser_manual() {
    MAIN_THREAD_ASYNC_EM_ASM({ Module.browserFiles.showManual(); });
}

void service_browser_assets(document_history_t& history, const std::filesystem::path& asset_base,
                            std::uint64_t session, bool loaded, bool busy) {
    if (!watching.load()) return;
    asset_snapshot_key_t key{document_stamp_t::from(history.document), asset_base, session, loaded, busy};
    if (snapshot.key != key) publish_assets(key, history.document);
    std::optional<std::string> queued;
    {
        std::lock_guard lock(request_mutex);
        queued = std::exchange(request, std::nullopt);
    }
    if (!queued) return;
    process_repair(*queued, history, key);
    key.document = document_stamp_t::from(history.document);
    if (snapshot.key != key) publish_assets(std::move(key), history.document);
}
} // namespace wormedit::platform

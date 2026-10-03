#include "ui/nuklear/sound_import_state.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t maximum_name_bytes = 128;

auto lower_extension(const std::string& path) -> std::string {
    auto extension = std::filesystem::path{path}.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    });
    return extension;
}

auto is_stem_friendly(const std::string& extension) -> bool {
    return extension == ".wav" || extension == ".ogg" || extension == ".flac";
}

auto display_name(const std::string& path) -> std::string {
    auto name = std::filesystem::path{path}.stem().string();
    if (name.size() > maximum_name_bytes) name.resize(maximum_name_bytes);
    return name;
}

auto trimmed(std::string text) -> std::string {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.back())) text.pop_back();
    std::size_t begin = 0;
    while (begin < text.size() && blank(text[begin])) ++begin;
    text.erase(0, begin);
    return text;
}

void choose_phase(worm_ui_sound_import_state_t& state, bool chart_has_bgm) {
    if (state.items.empty()) {
        state.phase = worm_ui_sound_import_phase_t::closed;
        return;
    }
    if (state.items.size() > 1) {
        state.phase = worm_ui_sound_import_phase_t::review_list;
        return;
    }
    const bool ambiguous = !chart_has_bgm &&
                           is_stem_friendly(lower_extension(state.items.front().path));
    state.phase = ambiguous ? worm_ui_sound_import_phase_t::choose_kind
                            : worm_ui_sound_import_phase_t::ready;
}

auto default_kind(const std::string& path, bool chart_has_bgm) -> std::uint8_t {
    return !chart_has_bgm && is_stem_friendly(lower_extension(path))
               ? WORM_UI_SOUND_IMPORT_STEM
               : WORM_UI_SOUND_IMPORT_KEYSOUND;
}

auto valid_kind(std::uint8_t kind) -> bool {
    return kind >= WORM_UI_SOUND_IMPORT_STEM && kind <= WORM_UI_SOUND_IMPORT_SAMPLE;
}

} // namespace

auto worm_ui_nuklear_sound_import_is_audio_path(const std::string& path) -> bool {
    static constexpr std::array<std::string_view, 4> extensions{".wav", ".ogg", ".flac", ".mp3"};
    const auto extension = lower_extension(path);
    return std::ranges::find(extensions, extension) != extensions.end();
}

auto worm_ui_nuklear_sound_import_add(worm_ui_sound_import_state_t& state,
                                      std::span<const std::string> paths, bool chart_has_bgm)
    -> std::size_t {
    state.message.clear();
    std::size_t added = 0;
    std::size_t skipped_type = 0;
    std::size_t skipped_duplicate = 0;
    std::size_t skipped_capacity = 0;
    for (const auto& path : paths) {
        if (!worm_ui_nuklear_sound_import_is_audio_path(path)) {
            ++skipped_type;
        } else if (std::ranges::any_of(state.items, [&](const auto& item) {
                       return item.path == path;
                   })) {
            ++skipped_duplicate;
        } else if (state.items.size() >= worm_ui_sound_import_capacity) {
            ++skipped_capacity;
        } else {
            state.items.push_back({path, display_name(path), default_kind(path, chart_has_bgm)});
            ++added;
        }
    }
    choose_phase(state, chart_has_bgm);
    if (skipped_type != 0)
        state.message = std::to_string(skipped_type) + " file(s) skipped: not wav, ogg, flac or mp3.";
    else if (skipped_duplicate != 0)
        state.message = std::to_string(skipped_duplicate) + " file(s) skipped: already in the list.";
    else if (skipped_capacity != 0)
        state.message = std::to_string(skipped_capacity) + " file(s) skipped: the list holds 1000.";
    return added;
}

void worm_ui_nuklear_sound_import_set_kind(worm_ui_sound_import_state_t& state,
                                           std::size_t index, std::uint8_t kind) {
    if (index < state.items.size() && valid_kind(kind)) state.items[index].kind = kind;
}

void worm_ui_nuklear_sound_import_set_all_kinds(worm_ui_sound_import_state_t& state,
                                                std::uint8_t kind) {
    if (!valid_kind(kind)) return;
    for (auto& item : state.items) item.kind = kind;
}

void worm_ui_nuklear_sound_import_rename(worm_ui_sound_import_state_t& state, std::size_t index,
                                         std::string name) {
    if (index >= state.items.size()) return;
    if (name.size() > maximum_name_bytes) name.resize(maximum_name_bytes);
    state.items[index].name = std::move(name);
}

void worm_ui_nuklear_sound_import_remove(worm_ui_sound_import_state_t& state, std::size_t index) {
    if (index >= state.items.size()) return;
    state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(index));
    // A list that shrinks to nothing closes; one left over stays a review list.
    if (state.items.empty()) state.phase = worm_ui_sound_import_phase_t::closed;
}

void worm_ui_nuklear_sound_import_move(worm_ui_sound_import_state_t& state, std::size_t index,
                                       int delta) {
    if (index >= state.items.size() || (delta != -1 && delta != 1)) return;
    if (delta < 0 && index == 0) return;
    const auto target = delta < 0 ? index - 1 : index + 1;
    if (target >= state.items.size()) return;
    std::swap(state.items[index], state.items[target]);
}

auto worm_ui_nuklear_sound_import_confirm(worm_ui_sound_import_state_t& state)
    -> std::vector<worm_ui_sound_import_item_t> {
    state.message.clear();
    if (state.items.empty()) return {};
    for (auto& item : state.items) item.name = trimmed(std::move(item.name));
    if (std::ranges::any_of(state.items, [](const auto& item) { return item.name.empty(); })) {
        state.message = "Every sound needs a name.";
        return {};
    }
    auto taken = std::move(state.items);
    state = {};
    return taken;
}

void worm_ui_nuklear_sound_import_cancel(worm_ui_sound_import_state_t& state) {
    state = {};
}

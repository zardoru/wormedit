#include "common/file_dialog.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <emscripten.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Use the pinned static SDL backend's drop producers, which own the queued path strings.
extern "C" bool SDL_SendDropPosition(SDL_Window* window, float x, float y);
extern "C" bool SDL_SendDropFile(SDL_Window* window, const char* source, const char* path);
extern "C" bool SDL_SendDropComplete(SDL_Window* window);

namespace {
struct browser_dialog_t {
    SDL_DialogFileCallback callback;
    void* data;
};

struct browser_drop_t {
    float x;
    float y;
    bool begin;
    std::vector<std::string> paths;
};

void dispatch_drop(void* data) {
    const std::unique_ptr<browser_drop_t> drop{static_cast<browser_drop_t*>(data)};
    const auto windows = SDL_GetWindows(nullptr);
    if (!windows) return;
    SDL_Window* window = windows[0];
    SDL_free(windows);
    if (!window) return;
    if (drop->begin) {
        SDL_SendDropPosition(window, drop->x, drop->y);
        return;
    }
    for (const auto& path : drop->paths) SDL_SendDropFile(window, nullptr, path.c_str());
    SDL_SendDropComplete(window);
}

void show_dialog(SDL_DialogFileCallback callback, void* data, int kind,
                 const char* location, bool multiple) {
    auto* request = new browser_dialog_t{callback, data};
    MAIN_THREAD_EM_ASM({
        Module.browserFiles.showDialog($0, $1, UTF8ToString($2), !!$3);
    }, request, kind, location ? location : "", multiple);
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE bool wormedit_browser_drop_files(float x, float y, const char* payload) {
    auto drop = std::make_unique<browser_drop_t>(browser_drop_t{.x = x, .y = y, .begin = !payload, .paths = {}});
    if (payload) {
        try {
            drop->paths = nlohmann::json::parse(payload).get<std::vector<std::string>>();
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    }
    auto* data = drop.release();
    if (SDL_RunOnMainThread(dispatch_drop, data, false)) return true;
    delete data;
    return false;
}

extern "C" EMSCRIPTEN_KEEPALIVE void wormedit_browser_dialog_complete(
    std::uintptr_t token, const char* payload, int success) {
    const std::unique_ptr<browser_dialog_t> request{reinterpret_cast<browser_dialog_t*>(token)};
    if (success < 0) {
        SDL_SetError("Browser file operation failed: %s", payload);
        request->callback(request->data, nullptr, -1);
        return;
    }
    std::vector<std::string> names;
    try {
        names = nlohmann::json::parse(payload).get<std::vector<std::string>>();
    } catch (...) {
        SDL_SetError("Browser file list was invalid.");
        request->callback(request->data, nullptr, -1);
        return;
    }
    std::vector<const char*> paths;
    for (const auto& name : names) paths.push_back(name.c_str());
    paths.push_back(nullptr);
    request->callback(request->data, paths.data(), -1);
}

namespace wormedit::platform {
void show_open_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window*,
    const SDL_DialogFileFilter*, int, const char* location, bool multiple) {
    show_dialog(callback, data, 0, location, multiple);
}
void show_save_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window*,
    const SDL_DialogFileFilter*, int, const char* location) {
    show_dialog(callback, data, 1, location, false);
}
void show_open_folder_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window*,
    const char* location, bool multiple) {
    show_dialog(callback, data, 2, location, multiple);
}
void download_browser_project(const char* path) {
    MAIN_THREAD_EM_ASM({ Module.browserFiles.downloadProject(UTF8ToString($0)); }, path);
}
} // namespace wormedit::platform

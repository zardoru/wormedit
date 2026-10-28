#include "common/file_dialog.h"

#include <SDL3/SDL_error.h>
#include <emscripten.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
struct browser_dialog_t {
    SDL_DialogFileCallback callback;
    void* data;
};

void show_dialog(SDL_DialogFileCallback callback, void* data, int kind,
                 const char* location, bool multiple) {
    auto* request = new browser_dialog_t{callback, data};
    MAIN_THREAD_EM_ASM({
        Module.browserFiles.showDialog($0, $1, UTF8ToString($2), !!$3);
    }, request, kind, location ? location : "", multiple);
}
} // namespace

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

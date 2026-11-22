#pragma once

#include <SDL3/SDL_dialog.h>

namespace wormedit::platform {

#ifdef __EMSCRIPTEN__
void show_open_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const SDL_DialogFileFilter* filters, int count, const char* location, bool multiple);
void show_save_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const SDL_DialogFileFilter* filters, int count, const char* location);
void show_open_folder_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const char* location, bool multiple);
void download_browser_project(const char* path);
#else
inline void show_open_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const SDL_DialogFileFilter* filters, int count, const char* location, bool multiple) {
    SDL_ShowOpenFileDialog(callback, data, window, filters, count, location, multiple);
}
inline void show_save_file_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const SDL_DialogFileFilter* filters, int count, const char* location) {
    SDL_ShowSaveFileDialog(callback, data, window, filters, count, location);
}
inline void show_open_folder_dialog(SDL_DialogFileCallback callback, void* data, SDL_Window* window,
    const char* location, bool multiple) {
    SDL_ShowOpenFolderDialog(callback, data, window, location, multiple);
}
#endif

} // namespace wormedit::platform

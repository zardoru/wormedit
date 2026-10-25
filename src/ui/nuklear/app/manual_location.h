#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit::ui::nuklear {
// The packaged manual lives beside the executable, including in the build tree.
class manual_location_t {
  public:
    static auto from(std::string_view application_directory) -> std::optional<manual_location_t> {
        if (application_directory.empty() || application_directory.find('\0') != std::string_view::npos)
            return std::nullopt;
        std::string path{application_directory};
        std::ranges::replace(path, '\\', '/');
        if (path.back() != '/') path += '/';
        path += "manual/index.html";
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return std::nullopt;
        std::string url = path.front() == '/' ? "file://" : "file:///";
        constexpr std::string_view hex = "0123456789ABCDEF";
        for (const unsigned char byte : path) {
            if (is_path_literal(byte)) url += static_cast<char>(byte);
            else {
                url += '%'; url += hex[byte >> 4]; url += hex[byte & 15];
            }
        }
        return manual_location_t{std::move(url)};
    }
    auto url() const -> const std::string& { return url_; }
    auto open() const -> bool { return SDL_OpenURL(url_.c_str()); }

  private:
    explicit manual_location_t(std::string url) : url_(std::move(url)) {}
    static auto is_path_literal(unsigned char byte) -> bool {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || std::string_view{"/-._~:"}.find(byte) != std::string_view::npos;
    }
    std::string url_;
};
}

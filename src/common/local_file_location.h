#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit {
// Local files and folders share URL escaping before handing off to the OS.
class local_file_location_t {
  public:
    static auto from(const std::filesystem::path& path, SDL_PathType type)
        -> std::optional<local_file_location_t> {
        if (path.empty()) return std::nullopt;
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if (error) return std::nullopt;
        const auto bytes = absolute.generic_u8string();
        const std::string text{bytes.begin(), bytes.end()};
        if (text.find('\0') != std::string::npos) return std::nullopt;
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(text.c_str(), &info) || info.type != type) return std::nullopt;
        std::string url = text.starts_with("//") ? "file:" :
                          text.front() == '/' ? "file://" : "file:///";
        constexpr std::string_view hex = "0123456789ABCDEF";
        for (const unsigned char byte : text) {
            if (is_path_literal(byte)) url += static_cast<char>(byte);
            else {
                url += '%'; url += hex[byte >> 4]; url += hex[byte & 15];
            }
        }
        return local_file_location_t{std::move(url)};
    }
    auto url() const -> const std::string& { return url_; }
    auto open() const -> bool { return SDL_OpenURL(url_.c_str()); }

  private:
    explicit local_file_location_t(std::string url) : url_(std::move(url)) {}
    static auto is_path_literal(unsigned char byte) -> bool {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || std::string_view{"/-._~:"}.find(byte) != std::string_view::npos;
    }
    std::string url_;
};
} // namespace wormedit

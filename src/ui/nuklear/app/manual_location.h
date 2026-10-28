#pragma once

#include "common/local_file_location.h"
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
        auto location = local_file_location_t::from(path, SDL_PATHTYPE_FILE);
        if (!location) return std::nullopt;
        return manual_location_t{std::move(*location)};
    }
    auto url() const -> const std::string& { return location_.url(); }
    auto open() const -> bool { return location_.open(); }

  private:
    explicit manual_location_t(local_file_location_t location) : location_(std::move(location)) {}
    local_file_location_t location_;
};
}

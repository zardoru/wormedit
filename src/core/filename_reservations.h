#pragma once
#include "text_util.h"
#include <filesystem>
#include <set>
#include <string>

namespace wormedit {
// One case-insensitive basename reservation owner for detached generated-file plans.
class filename_reservations_t {
  public:
    auto reserve(const std::filesystem::path& path) -> bool {
        return occupied_.insert(text_util::ascii_lowercase(path.filename().generic_string())).second;
    }
    template<class NextCandidate>
    auto allocate(NextCandidate next) -> std::filesystem::path {
        std::filesystem::path candidate;
        do { candidate = next(); } while (!reserve(candidate));
        return candidate;
    }
  private:
    std::set<std::string> occupied_;
};
}

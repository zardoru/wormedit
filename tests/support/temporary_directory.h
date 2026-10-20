#pragma once
#include <wormedit/core/result.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace wormedit::test {
class temporary_directory_t {
  public:
    [[nodiscard]] static auto from() -> result_t<temporary_directory_t, std::string> {
        static std::atomic<unsigned> sequence{};
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) return std::unexpected(error.message());
        for (int attempt = 0; attempt < 32; ++attempt) {
            auto path = root / ("wormedit-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1)));
            if (std::filesystem::create_directory(path, error)) return temporary_directory_t{std::move(path)};
            if (error) return std::unexpected(error.message());
        }
        return std::unexpected("Could not create a temporary test directory.");
    }
    temporary_directory_t(temporary_directory_t&& other) noexcept
        : path_(std::exchange(other.path_, std::filesystem::path{})) {}
    ~temporary_directory_t() {
        if (path_.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }
  private:
    explicit temporary_directory_t(std::filesystem::path path) : path_(std::move(path)) {}
    std::filesystem::path path_;
};
}

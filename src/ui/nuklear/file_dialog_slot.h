#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
namespace wormedit::ui {
struct file_dialog_result_t {
    std::string path;
    std::string error;
    bool canceled{};
};
class file_dialog_slot_t {
  public:
    auto begin() -> std::optional<std::uint64_t> {
        std::lock_guard lock{mutex_};
        if (open_ || generation_ == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        ++generation_;
        open_ = true;
        result_.reset();
        return generation_;
    }
    void complete(std::uint64_t generation, std::string&& path, std::string&& error,
                  bool canceled) {
        std::lock_guard lock{mutex_};
        if (!open_ || generation != generation_)
            return;
        open_ = false;
        result_ = {.path = std::move(path), .error = std::move(error), .canceled = canceled};
    }
    auto take() -> std::optional<file_dialog_result_t> {
        std::lock_guard lock{mutex_};
        auto result = std::move(result_);
        result_.reset();
        return result;
    }
    void reset() {
        std::lock_guard lock{mutex_};
        if (generation_ != std::numeric_limits<std::uint64_t>::max())
            ++generation_;
        open_ = false;
        result_.reset();
    }
    auto open() -> bool {
        std::lock_guard lock{mutex_};
        return open_;
    }

  private:
    std::mutex mutex_;
    std::uint64_t generation_{};
    bool open_{};
    std::optional<file_dialog_result_t> result_;
};
} // namespace wormedit::ui

#pragma once
#include <wormedit/core/result.h>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
namespace wormedit::ui {
struct file_dialog_choice_t {
    std::string path;
    bool canceled{};

    auto operator==(const file_dialog_choice_t&) const -> bool = default;
};
struct file_dialog_failure_t {
    file_dialog_choice_t choice;
    std::string message;

    auto operator==(const file_dialog_failure_t&) const -> bool = default;
};
using file_dialog_result_t = result_t<file_dialog_choice_t, file_dialog_failure_t>;
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
        file_dialog_choice_t choice{.path = std::move(path), .canceled = canceled};
        if (error.empty())
            result_.emplace(std::move(choice));
        else
            result_.emplace(std::unexpected{file_dialog_failure_t{
                .choice = std::move(choice), .message = std::move(error)}});
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

#include "ui/nuklear/compose_state.h"

#include <mutex>
#include <utility>

namespace {

struct compose_file_dialog_state_t {
    std::mutex mutex;
    std::uint64_t generation{};
    bool open{};
    std::optional<worm_ui_nuklear_compose_file_dialog_result_t> result;
};

auto compose_file_dialog_state() -> compose_file_dialog_state_t& {
    static compose_file_dialog_state_t state;
    return state;
}

} // namespace

auto worm_ui_nuklear_compose_file_dialog_begin() -> std::optional<std::uint64_t> {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (dialog.open || dialog.generation == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    ++dialog.generation;
    dialog.open = true;
    dialog.result.reset();
    return dialog.generation;
}

void worm_ui_nuklear_compose_file_dialog_complete(
    std::uint64_t generation, std::string path, std::string error, bool canceled) {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (!dialog.open || generation != dialog.generation) {
        return;
    }
    dialog.open = false;
    dialog.result = {.path = std::move(path),
                     .error = std::move(error),
                     .canceled = canceled};
}

auto worm_ui_nuklear_compose_file_dialog_take()
    -> std::optional<worm_ui_nuklear_compose_file_dialog_result_t> {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    auto result = std::move(dialog.result);
    dialog.result.reset();
    return result;
}

auto worm_ui_nuklear_compose_file_dialog_open() -> bool {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    return dialog.open;
}

auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t& {
    static worm_ui_nuklear_compose_state_t state;
    return state;
}

void worm_ui_nuklear_reset_compose_state() {
    auto& dialog = compose_file_dialog_state();
    {
        std::lock_guard lock{dialog.mutex};
        if (dialog.generation != std::numeric_limits<std::uint64_t>::max()) {
            ++dialog.generation;
        }
        dialog.open = false;
        dialog.result.reset();
    }
    worm_ui_nuklear_compose_state() = {};
}

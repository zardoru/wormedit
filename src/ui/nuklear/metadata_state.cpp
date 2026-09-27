#include "ui/nuklear/metadata_state.h"

#include <mutex>
#include <utility>

namespace {

struct metadata_file_dialog_state_t {
    std::mutex mutex;
    std::uint64_t generation{};
    bool open{};
    std::optional<worm_ui_nuklear_metadata_file_dialog_result_t> result;
};

auto metadata_file_dialog_state() -> metadata_file_dialog_state_t& {
    static metadata_file_dialog_state_t state;
    return state;
}

} // namespace

auto worm_ui_nuklear_metadata_file_dialog_begin() -> std::optional<std::uint64_t> {
    auto& dialog = metadata_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (dialog.open || dialog.generation == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    ++dialog.generation;
    dialog.open = true;
    dialog.result.reset();
    return dialog.generation;
}

void worm_ui_nuklear_metadata_file_dialog_complete(std::uint64_t generation, std::string path,
                                                   std::string error, bool canceled) {
    auto& dialog = metadata_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (!dialog.open || generation != dialog.generation)
        return;
    dialog.open = false;
    dialog.result = {.path = std::move(path), .error = std::move(error), .canceled = canceled};
}

auto worm_ui_nuklear_metadata_file_dialog_take()
    -> std::optional<worm_ui_nuklear_metadata_file_dialog_result_t> {
    auto& dialog = metadata_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    auto result = std::move(dialog.result);
    dialog.result.reset();
    return result;
}

auto worm_ui_nuklear_metadata_file_dialog_open() -> bool {
    auto& dialog = metadata_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    return dialog.open;
}

auto worm_ui_nuklear_metadata_state() noexcept -> worm_ui_nuklear_metadata_state_t& {
    static worm_ui_nuklear_metadata_state_t state;
    return state;
}

void worm_ui_nuklear_clear_metadata_drop_targets() {
    auto& state = worm_ui_nuklear_metadata_state();
    state.project_drop_rectangle = {};
    state.export_drop_rectangle = {};
    state.export_drop_group_index = 0;
    state.output_path_drop_rectangle = {};
    state.output_path_drop_group_index = 0;
}

void worm_ui_nuklear_set_metadata_drop_target(
    WormUiMetadataDropTarget target, worm_ui_nuklear_metadata_state_t::drop_rectangle_t rect) {
    auto& state = worm_ui_nuklear_metadata_state();
    if (target.destination == WORM_UI_METADATA_DROP_PROJECT) {
        state.project_drop_rectangle = rect;
    } else if (target.destination == WORM_UI_METADATA_DROP_EXPORT) {
        state.export_drop_rectangle = rect;
        state.export_drop_group_index = target.output_group_index;
    } else if (target.destination == WORM_UI_METADATA_DROP_OUTPUT_PATH) {
        state.output_path_drop_rectangle = rect;
        state.output_path_drop_group_index = target.output_group_index;
    }
}

auto worm_ui_nuklear_metadata_drop_target(float x, float y) -> WormUiMetadataDropTarget {
    const auto& state = worm_ui_nuklear_metadata_state();
    const auto contains = [x, y](const auto& rect) {
        return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
               x < rect.x + rect.width && y < rect.y + rect.height;
    };
    if (contains(state.export_drop_rectangle))
        return {WORM_UI_METADATA_DROP_EXPORT, state.export_drop_group_index};
    if (contains(state.output_path_drop_rectangle))
        return {WORM_UI_METADATA_DROP_OUTPUT_PATH, state.output_path_drop_group_index};
    if (contains(state.project_drop_rectangle))
        return {WORM_UI_METADATA_DROP_PROJECT, 0};
    return {};
}

void worm_ui_nuklear_reset_metadata_state() {
    auto& dialog = metadata_file_dialog_state();
    {
        std::lock_guard lock{dialog.mutex};
        if (dialog.generation != std::numeric_limits<std::uint64_t>::max())
            ++dialog.generation;
        dialog.open = false;
        dialog.result.reset();
    }
    worm_ui_nuklear_metadata_state() = {};
}

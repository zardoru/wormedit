#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/preferences.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Process;

namespace wormedit::sys {

using external_player_definition_t = wormedit::external_player_definition_t;
using external_player_preferences_t = wormedit::external_player_preferences_t;

using external_player_arguments_t = result_t<std::vector<std::string>, std::string>;

// Tokenize before substitution: spaces in substituted paths cannot create arguments.
// Single/double quotes group arguments; backslashes are literal (including Windows paths).
[[nodiscard]] auto expand_external_player_arguments(std::string_view argument_template,
    std::string_view application_path, std::string_view filename, std::uint64_t source_measure)
    -> external_player_arguments_t;
// Missing preferences retain the defaults. Malformed input never changes preferences.
[[nodiscard]] auto load_external_player_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) -> std::string;
[[nodiscard]] auto save_external_player_preferences(const std::filesystem::path& path,
    const external_player_preferences_t& preferences) -> std::string;

enum class external_player_action_t { play_start, play_here, stop };
struct external_player_launch_result_t {
    [[nodiscard]] auto is_success() const noexcept -> bool { return launched; }

    bool launched{};
    bool open_preferences{};
    std::string error;
    std::filesystem::path preview_path;
};


class external_player_t {
public:
    external_player_t() = default;
    external_player_t(const external_player_t&) = delete;
    auto operator=(const external_player_t&) -> external_player_t& = delete;
    ~external_player_t();
    [[nodiscard]] auto launch(const external_player_definition_t& player,
        external_player_action_t action, const song_document_t& song, chart_id_t chart,
        const std::filesystem::path& source_path, std::uint64_t source_measure,
        const std::filesystem::path& application_path) -> external_player_launch_result_t;
    void cleanup() noexcept;
private:
    struct temporary_file_t {
        std::filesystem::path path;
        std::uint64_t identity_volume{};
        std::uint64_t identity_file{};
        SDL_Process* process{};
    };
    std::vector<temporary_file_t> temporary_files_;
    void remove_temporary(temporary_file_t& temporary) noexcept;
    auto prepare_preview(const song_document_t& song, chart_id_t chart,
        const std::filesystem::path& source) -> external_player_launch_result_t;
};

} // namespace wormedit::sys

#pragma once

#include "sys/preferences/preferences_runtime.h"
#include "sys/updates/update_checker.h"
#include "ui/frontend.h"

#include <functional>
#include <memory>

namespace wormedit::sys {

// Where the stable channel is published (written by release-pages.yml next to the browser build).
inline constexpr std::string_view default_update_manifest_url = "https://zardoru.github.io/wormedit/latest.json";

struct update_service_options_t {
    std::string current_version;
    std::string manifest_url{default_update_manifest_url};
    std::filesystem::path state_path;
    notification_sink_t notifications{notification_sink_t::muted()};
    manifest_fetch_t fetch; // Empty: this build cannot check, and the service stays inert.
    std::function<void(std::string_view)> open_url;
};

// The application's side of the update check and the first-run questions: owns the checker,
// answers the shell's requests, and projects its state into the frame view.
class update_service_t {
  public:
    explicit update_service_t(update_service_options_t options);

    [[nodiscard]] auto is_available() const -> bool { return checker_ != nullptr; }
    // Launch-time check when enabled and no first-run question is pending.
    void start_if_enabled(const program_preferences_t&, std::int64_t now_unix_seconds);
    // Once per frame: copies the checker state so the view can borrow its text.
    void refresh(const program_preferences_t&);
    void fill_view(ui::updates_view_t&) const;
    // Handles this frame's request. Preference changes go through apply_program_preferences.
    void handle(const ui::updates_request_t&, program_preferences_runtime_t&, std::int64_t now_unix_seconds);

  private:
    update_service_options_t options_;
    std::unique_ptr<update_checker_t> checker_;
    update_status_t status_;
    std::string first_run_step_;
};

} // namespace wormedit::sys

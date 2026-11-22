#include "sys/updates/update_service.h"

#include <array>
#include <span>

namespace wormedit::sys {

update_service_t::update_service_t(update_service_options_t options) : options_(std::move(options)) {
    if (!options_.fetch) return;
    checker_ = std::make_unique<update_checker_t>(update_checker_options_t{
        .current_version = options_.current_version, .manifest_url = options_.manifest_url,
        .state_path = options_.state_path, .fetch = options_.fetch, .notifications = options_.notifications});
}

void update_service_t::start_if_enabled(const program_preferences_t& preferences, std::int64_t now_unix_seconds) {
    if (checker_ && preferences.updates.check_on_startup && next_first_run_step(preferences.first_run).empty())
        (void)checker_->start(false, now_unix_seconds);
}

void update_service_t::refresh(const program_preferences_t& preferences) {
    static constexpr std::array<std::string_view, 1> without_network{"updates"};
    first_run_step_ = std::string{next_first_run_step(
        preferences.first_run, checker_ ? std::span<const std::string_view>{} : std::span{without_network})};
    if (checker_) status_ = checker_->status();
}

void update_service_t::fill_view(ui::updates_view_t& view) const {
    view.first_run_step = first_run_step_;
    view.check_available = checker_ != nullptr;
    view.checking = status_.phase == update_status_t::phase_t::checking;
    view.update_available = status_.phase == update_status_t::phase_t::available;
    if (!view.update_available) return;
    view.latest_version = status_.latest.version;
    view.download_url = status_.latest.url;
    view.notes_url = status_.latest.notes_url;
}

void update_service_t::handle(const ui::updates_request_t& request, program_preferences_runtime_t& runtime,
                              std::int64_t now_unix_seconds) {
    using kind_t = ui::updates_request_t::kind_t;
    switch (request.kind) {
    case kind_t::none: return;
    case kind_t::check_now:
        if (checker_) (void)checker_->start(true, now_unix_seconds);
        return;
    case kind_t::open_download:
        if (options_.open_url && status_.phase == update_status_t::phase_t::available)
            options_.open_url(status_.latest.url);
        return;
    case kind_t::first_run_answer: {
        auto candidate = runtime.preferences;
        if (!complete_first_run_step(candidate.first_run, request.step)) return;
        if (request.step == "updates") candidate.updates.check_on_startup = request.choice != "no";
        const auto error = apply_program_preferences(runtime, std::move(candidate));
        if (!error.empty())
            options_.notifications.post(notification_severity_t::warning, notification_source_t::system, error);
        start_if_enabled(runtime.preferences, now_unix_seconds);
        return;
    }
    }
}

} // namespace wormedit::sys

#pragma once

#include "ui/frontend.h"
#include <wormedit/core/notifications.h>

namespace wormedit::sys {

class frontend_notification_sink_t {
    notification_sink_t sink_;
    static void post(void* context, WormUiNotificationSeverity severity,
                     WormUiNotificationProducerSource source, const char* message) noexcept {
        const auto producer = source == WORM_UI_NOTIFICATION_PRODUCER_INPUT
            ? notification_source_t::input : notification_source_t::system;
        auto& self = *static_cast<frontend_notification_sink_t*>(context);
        self.sink_.post(static_cast<notification_severity_t>(severity), producer, message);
    }
public:
    explicit frontend_notification_sink_t(notification_sink_t sink) noexcept : sink_(sink) {
        worm_ui_frontend_set_notification_sink(this, post);
    }
    ~frontend_notification_sink_t() { worm_ui_frontend_set_notification_sink(nullptr, nullptr); }
    frontend_notification_sink_t(const frontend_notification_sink_t&) = delete;
    auto operator=(const frontend_notification_sink_t&) -> frontend_notification_sink_t& = delete;
};

} // namespace wormedit::sys

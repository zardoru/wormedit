#pragma once

#include <wormedit/core/result.h>

#include <memory>
#include <string>
#include <utility>

namespace wormedit::visual {

template <class Context, class Payload> struct visual_job_error_t {
    Context context;
    std::shared_ptr<const Payload> payload;
    error_t<failure_code_t> cause;
};

template <class Context, class Payload> struct visual_job_value_t {
    [[nodiscard]] static auto from(Context context,
                                   std::shared_ptr<const Payload> payload, std::string message = {})
        -> result_t<visual_job_value_t, visual_job_error_t<Context, Payload>> {
        if (payload != nullptr)
            return visual_job_value_t{.context = std::move(context), .payload = std::move(payload),
                                       .message = std::move(message)};
        return std::unexpected(visual_job_error_t<Context, Payload>{
            .context = std::move(context), .payload = std::move(payload),
            .cause = {failure_code_t::invalid_request, std::move(message)}});
    }

    Context context;
    std::shared_ptr<const Payload> payload;
    std::string message;
};

} // namespace wormedit::visual

#pragma once

#include <wormedit/core/result.h>

#include <memory>
#include <string>
#include <utility>

namespace wormedit::visual {

template <class Context, class Payload, class Code> struct visual_job_error_t {
    Context context;
    std::shared_ptr<const Payload> payload;
    error_t<Code> cause;
};

template <class Context, class Payload, class Code> struct visual_job_value_t {
    [[nodiscard]] static auto can_publish(Code code, const std::shared_ptr<const Payload>& payload)
        noexcept -> bool {
        return code == Code::ready && payload != nullptr;
    }

    [[nodiscard]] static auto from(Context context, Code code,
                                   std::shared_ptr<const Payload> payload = {}, std::string message = {})
        -> result_t<visual_job_value_t, visual_job_error_t<Context, Payload, Code>> {
        if (can_publish(code, payload))
            return visual_job_value_t{.context = std::move(context), .payload = std::move(payload),
                                       .message = std::move(message)};
        return std::unexpected(visual_job_error_t<Context, Payload, Code>{
            .context = std::move(context), .payload = std::move(payload),
            .cause = {code, std::move(message)}});
    }

    Context context;
    std::shared_ptr<const Payload> payload;
    std::string message;
};

} // namespace wormedit::visual

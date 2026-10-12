#pragma once
#include <cstdint>
#include <wormedit/audio/transport.h>
namespace wormedit::audio {
template <class Status> struct session_render_result_t {
    Status status{Status::stopped};
    std::uint64_t first_timeline_frame{};
    transport_mix_result_t mix;
};
} // namespace wormedit::audio

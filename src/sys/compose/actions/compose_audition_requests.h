#pragma once

#include <wormedit/core/audition_request.h>
#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>

#include <array>
#include <optional>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/audio/chart_preview_state.h"
#include "sys/audio/project_audio_sources.h"
#include "sys/audio/sdl_audition_output.h"
#include "sys/audio/sdl_preview_output.h"
#include <wormedit/audio/object_audition.h>
#endif

namespace wormedit::sys {

// Borrowed only for one synchronous audition scope; actual audio resources stay
// with the application audio runtime.
struct compose_audition_inputs_t {
    document_history_t& history;
    const notification_sink_t& notifications;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    struct preview_t {
        audio::preview_session_t& session;
        sdl_preview_output_t& output;
        runtime::chart_preview_t& state;
    } preview;
    struct audition_t {
        project_audio_sources_t& sources;
        audio::object_audition_controller_t& controller;
        sdl_audition_output_t& output;
    } audition;
#endif
};

class compose_auditions_t {
    enum class phase_t { input, catalog };

  public:
    class scope_t {
      public:
        scope_t(const scope_t&) = delete;
        auto operator=(const scope_t&) -> scope_t& = delete;
        ~scope_t();

      private:
        friend class compose_auditions_t;
        scope_t(compose_auditions_t& owner, compose_audition_inputs_t inputs, phase_t phase);
        compose_auditions_t& owner_;
        compose_audition_inputs_t inputs_;
        phase_t phase_;
    };

    void submit(audition_request_t request);
    [[nodiscard]] auto input_scope(compose_audition_inputs_t inputs) -> scope_t;
    [[nodiscard]] auto catalog_scope(compose_audition_inputs_t inputs) -> scope_t;
    [[nodiscard]] auto has_pending_request() const -> bool;
    [[nodiscard]] auto has_pending_request(const audition_request_t& request) const -> bool;

  private:
    using pending_t = std::array<std::optional<audition_request_t>,
        std::variant_size_v<audition_request_t>>;
    // Bounded coalescing preserves last-per-kind input requests and the catalog
    // request's existing next-frame phase. No render/project mailboxes escape.
    pending_t pending_;

    template<class Request> static consteval auto index() -> std::size_t {
        return audition_request_t{Request{}}.index();
    }
    void begin(phase_t phase);
    void finish(phase_t phase, const compose_audition_inputs_t& inputs);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    static void process_object_request(const editor_object_audition_request_t& request,
                                      const compose_audition_inputs_t& inputs);
    static void process_draft_request(const bmson_draft_audition_request_t& request,
                                     const compose_audition_inputs_t& inputs);
    static void process_catalog_request(resource_id_t request,
                                       const compose_audition_inputs_t& inputs);
#endif
};

} // namespace wormedit::sys

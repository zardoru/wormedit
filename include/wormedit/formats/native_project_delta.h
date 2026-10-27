#pragma once

#include <wormedit/formats/native_project.h>
#include <memory>

namespace wormedit {

// Accepted native values, keyed by identity, never command intent. An unsupported
// comparison is an error so the journal writer can persist a complete seed.
class native_project_delta_t {
  public:
    [[nodiscard]] static auto from(const song_document_t& before, const song_document_t& after)
        -> result_t<native_project_delta_t, error_t<failure_code_t>>;
    [[nodiscard]] static auto parse(std::string_view bytes, native_project_limits_t limits = {})
        -> result_t<native_project_delta_t, error_t<failure_code_t>>;
    [[nodiscard]] auto apply_on(const song_document_t&, native_project_limits_t limits = {}) const
        -> native_project_parse_outcome_t;
    [[nodiscard]] auto bytes() const -> std::string_view { return bytes_; }
    [[nodiscard]] auto is_empty() const -> bool { return is_empty_; }
    // A conservative lower bound on the next compact seed's bytes. Additions are
    // ignored; bytes replaced or removed are subtracted. Underestimation reseeds.
    [[nodiscard]] auto minimum_result_bytes(std::size_t base_bytes) const -> std::size_t;
  private:
    std::string bytes_;
    std::size_t removed_bytes_{};
    bool is_empty_{};
};

// A bounded encoded segment. Each patch is transactional; native graph validation
// happens only when finish() publishes the final snapshot. No domain values are encoded here.
class native_project_replay_t {
  public:
    [[nodiscard]] static auto from(std::string_view seed, native_project_limits_t limits = {})
        -> result_t<native_project_replay_t, error_t<failure_code_t>>;
    native_project_replay_t(native_project_replay_t&&) noexcept;
    auto operator=(native_project_replay_t&&) noexcept -> native_project_replay_t&;
    ~native_project_replay_t();
    [[nodiscard]] auto apply(const native_project_delta_t&) -> result_t<void, error_t<failure_code_t>>;
    [[nodiscard]] auto finish() const -> native_project_parse_outcome_t;
    [[nodiscard]] auto can_apply_delta() const -> bool;
    [[nodiscard]] auto project() const -> song_id_t;
    [[nodiscard]] auto revision() const -> std::uint64_t;
  private:
    struct storage_t;
    explicit native_project_replay_t(std::unique_ptr<storage_t>);
    std::unique_ptr<storage_t> storage_;
};

} // namespace wormedit

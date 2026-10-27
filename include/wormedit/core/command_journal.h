#pragma once

#include <wormedit/core/notifications.h>
#include <wormedit/formats/native_project_storage.h>
#include <memory>

namespace wormedit {

inline constexpr std::string_view command_journal_header =
    "{\"format\":\"wormedit-command-journal\",\"version\":2}\n";
inline constexpr std::string_view command_journal_v1_header =
    "{\"format\":\"wormedit-command-journal\",\"version\":1}\n";

struct command_journal_limits_t {
    static constexpr std::size_t maximum_supported_bytes = 128U * 1024U * 1024U;
    static constexpr std::size_t maximum_supported_record_bytes = 64U * 1024U * 1024U;
    static constexpr std::size_t maximum_supported_records = 1024;
    std::size_t maximum_bytes{maximum_supported_bytes};
    std::size_t maximum_record_bytes{maximum_supported_record_bytes};
    std::size_t maximum_records{maximum_supported_records};
    auto operator==(const command_journal_limits_t&) const -> bool = default;
};

struct command_journal_entry_t {
    std::uint64_t sequence{};
    std::uint64_t before_revision{};
    std::string command;
    native_project_recovery_snapshot_t snapshot;
};

struct command_journal_record_position_t {
    std::uint64_t sequence{};
    std::size_t end_byte{};
    bool is_seed{true};
    auto operator==(const command_journal_record_position_t&) const -> bool = default;
};

struct command_journal_read_t {
    [[nodiscard]] static auto has_header(std::string_view bytes) -> bool;
    [[nodiscard]] static auto from(std::string_view bytes, command_journal_limits_t limits = {},
        native_project_limits_t codec_limits = {})
        -> diagnosed_t<command_journal_read_t, std::vector<native_project_storage_diagnostic_t>>;
    [[nodiscard]] auto replay_on(const song_document_t& baseline) const -> diagnosed_t<song_document_t>;
    std::optional<native_project_recovery_snapshot_t> snapshot;
    std::uint64_t last_sequence{};
    std::string last_checksum;
    std::string last_command;
    std::uint64_t last_before_revision{};
    std::uint32_t format_version{2};
    std::size_t deltas_since_seed{};
    std::size_t minimum_seed_bytes{};
    std::size_t accepted_records{};
    std::size_t valid_bytes{};
    bool stopped_by_limit{};
    std::vector<command_journal_record_position_t> positions;
    std::vector<std::uint64_t> revisions;
};

// Single-writer durable append with cached validated history. Recovery reads
// verify every record and validate the final segment state; compaction uses the shared atomic publisher.
class command_journal_t {
  public:
    explicit command_journal_t(std::filesystem::path path, command_journal_limits_t limits = {});
    [[nodiscard]] auto read() const
        -> diagnosed_t<command_journal_read_t, std::vector<native_project_storage_diagnostic_t>>;
    [[nodiscard]] auto append(const command_journal_entry_t& entry) const -> result_t<void, error_t<failure_code_t>>;
    // Last state accepted by this writer; does not revalidate external file changes.
    [[nodiscard]] auto accepted_state() const -> const command_journal_read_t*;
    // Caller must have successfully published a snapshot covering this sequence.
    [[nodiscard]] auto checkpoint(std::uint64_t through_sequence) const -> result_t<void, std::string>;
  private:
    struct file_contents_t {
        std::string bytes;
        std::uint64_t source_size{};
    };
    struct validated_file_t {
        command_journal_read_t read;
        std::uint64_t source_size{};
    };
    [[nodiscard]] auto read_bytes() const -> result_t<file_contents_t, std::string>;
    std::filesystem::path path_;
    command_journal_limits_t limits_;
    mutable std::optional<validated_file_t> validated_;
    mutable bool requires_seed_{};
};

class task_scheduler_t;
struct native_project_autosave_target_t;

// Main-thread admission; serialization and publication run on async_job_t.
class command_journal_runtime_t {
  public:
    explicit command_journal_runtime_t(task_scheduler_t&, notification_sink_t = notification_sink_t::muted(),
        command_journal_limits_t = {});
    ~command_journal_runtime_t();
    void configure(song_id_t, const native_project_autosave_target_t&, std::uint64_t sequence_floor = 0);
    void record(const song_document_t&, std::uint64_t before_revision, std::string_view command);
    void mark_explicit_save(native_project_document_version_t);
    void poll();
    void flush();
    void drain();
    void reset(std::uint64_t initial_sequence = 0);
    void checkpoint(native_project_journal_position_t);
    [[nodiscard]] auto position() const -> std::optional<native_project_journal_position_t>;
    [[nodiscard]] auto requires_checkpoint() const -> bool;
    [[nodiscard]] auto is_durable() const -> bool;
    [[nodiscard]] auto is_busy() const -> bool;
    [[nodiscard]] auto target() const -> const native_project_autosave_target_t&;
  private:
    struct storage_t;
    std::unique_ptr<storage_t> storage_;
};

} // namespace wormedit

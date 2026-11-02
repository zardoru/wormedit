#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <functional>
#include <memory>
#include <stop_token>
#include <wormedit/core/result.h>
namespace wormedit {
using atomic_file_save_outcome_t = result_t<void, std::string>;
class prepared_atomic_file_t {
    struct storage_t;
  public:
    enum class seek_origin_t { begin, current, end };
    // Borrowed only for the producer callback. No path/native handle escapes;
    // writes and seeks enforce the same extent limit and cancellation token.
    class stream_t {
      public:
        stream_t(const stream_t&) = delete;
        auto operator=(const stream_t&) -> stream_t& = delete;
        [[nodiscard]] auto write(std::span<const std::byte> bytes) -> atomic_file_save_outcome_t;
        [[nodiscard]] auto seek(std::int64_t offset, seek_origin_t origin)
            -> result_t<std::int64_t, std::string>;
        [[nodiscard]] auto size() const noexcept -> std::uint64_t;
      private:
        friend class prepared_atomic_file_t;
        explicit stream_t(storage_t& storage) : storage_(storage) {}
        storage_t& storage_;
    };
    using producer_t = std::function<atomic_file_save_outcome_t(stream_t&)>;
    using validator_t = std::function<atomic_file_save_outcome_t(const std::filesystem::path&)>;
    [[nodiscard]] static auto from(const std::filesystem::path& path, std::string_view bytes,
        std::size_t maximum_bytes, validator_t validate = {}, std::stop_token stop = {})
        -> result_t<prepared_atomic_file_t, std::string>;
    [[nodiscard]] static auto from_stream(const std::filesystem::path& path,
        std::size_t maximum_bytes, producer_t produce, validator_t validate = {},
        std::stop_token stop = {}) -> result_t<prepared_atomic_file_t, std::string>;
    prepared_atomic_file_t(prepared_atomic_file_t&&) noexcept;
    auto operator=(prepared_atomic_file_t&&) noexcept -> prepared_atomic_file_t&;
    ~prepared_atomic_file_t();
    [[nodiscard]] auto publish(bool replace_existing = true) -> atomic_file_save_outcome_t;
  private:
    std::unique_ptr<storage_t> storage_;
    explicit prepared_atomic_file_t(std::unique_ptr<storage_t> storage);
};
// Exclusive same-directory temporary; complete write, file flush, close, atomic rename.
// Before successful publication, failures preserve the destination and remove only our temporary.
[[nodiscard]] auto create_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                                      std::size_t maximum_bytes) -> atomic_file_save_outcome_t;
struct file_closer_t {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
using unique_file_t = std::unique_ptr<std::FILE, file_closer_t>;
// Creates a new file for binary writing and fails if it already exists. Windows paths
// open through the wide API so non-ASCII names work.
[[nodiscard]] auto open_exclusive_file(const std::filesystem::path& path) -> unique_file_t;
} // namespace wormedit

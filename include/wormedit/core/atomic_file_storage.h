#pragma once
#include <cstddef>
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
  public:
    using validator_t = std::function<atomic_file_save_outcome_t(const std::filesystem::path&)>;
    [[nodiscard]] static auto from(const std::filesystem::path& path, std::string_view bytes,
        std::size_t maximum_bytes, validator_t validate = {}, std::stop_token stop = {})
        -> result_t<prepared_atomic_file_t, std::string>;
    prepared_atomic_file_t(prepared_atomic_file_t&&) noexcept;
    auto operator=(prepared_atomic_file_t&&) noexcept -> prepared_atomic_file_t&;
    ~prepared_atomic_file_t();
    [[nodiscard]] auto publish(bool replace_existing = true) -> atomic_file_save_outcome_t;
  private:
    struct storage_t;
    std::unique_ptr<storage_t> storage_;
    explicit prepared_atomic_file_t(std::unique_ptr<storage_t> storage);
};
// Exclusive same-directory temporary; complete write, file flush, close, atomic rename.
// Before successful publication, failures preserve the destination and remove only our temporary.
[[nodiscard]] auto create_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                                      std::size_t maximum_bytes) -> atomic_file_save_outcome_t;
} // namespace wormedit
